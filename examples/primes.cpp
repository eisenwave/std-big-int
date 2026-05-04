#include <beman/big_int/big_int.hpp>

#include <chrono>
#include <iostream>
#include <random>

#define BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY)
template <class BuiltInUintType>
auto current_time_stamp_now() noexcept -> BuiltInUintType {

    const auto current_now =
        static_cast<std::uintmax_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count());

    return static_cast<BuiltInUintType>(current_now);
}
#endif // BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY

template <class BigIntType, class RndEngineType>
auto get_pseudo_random_integer(BigIntType& value_to_get, const BigIntType& max_val, RndEngineType& eng) -> BigIntType {
    using distribution_type = std::uniform_int_distribution<std::uint64_t>;

    distribution_type dist{std::uint64_t{UINT64_C(0x1000000000000001)}, std::uint64_t{UINT64_C(0xFFFFFFFFFFFFFFFF)}};

    value_to_get = BigIntType{};

    for (int bit_index{0}; value_to_get < max_val; bit_index += 64) {
        if (bit_index != 0U) {
            value_to_get <<= 64U;
        }

        value_to_get += dist(eng);
    }

    if ((static_cast<unsigned>(value_to_get) & 1U) == 0U) {
        // Ensure that the candidate is odd.
        value_to_get += 1;
    }

    return value_to_get % max_val;
}

template <class BigIntType>
auto powm(BigIntType b, BigIntType p, const BigIntType& m) -> BigIntType {

    // Calculate (b ^ p) % m.

    BigIntType result{};

    BigIntType x{1};

    unsigned p0{};

    while (((p0 = static_cast<unsigned>(p)) != 0U) || (p != 0U)) {
        if ((p0 & 1U) != 0U) {
            x *= b;
            x %= m;
        }

        b *= b;
        b %= m;

        p >>= 1U;
    }

    result = x;

    return result;
}

auto lsb_position(std::uint64_t x) -> unsigned {
    unsigned pos{};

    while (static_cast<unsigned>(x & UINT64_C(1)) == 0U) {
        x >>= 1U;
        ++pos;
    }

    return pos;
}

template <class BigIntType, class RndEngineType>
auto miller_rabin(const BigIntType& np, const int trials, RndEngineType& eng) -> bool {
    // Perform the Miller-Rabin primality test on the prime candidate np.
    // This subroutine returns true if the prime candidate is prime within
    // the limits of Miller-Rabin testing for the given input of trials.
    // In this reduced implementation, we ignore small primes.

    const BigIntType nm1{np - static_cast<unsigned>(UINT8_C(1))};

    auto local_functor_isone{[](const BigIntType& t1) { return ((static_cast<unsigned>(t1) == 1U) && (t1 == 1U)); }};

    {
        // Perform a single Fermat test which will exclude many non-prime candidates.
        // If this fails, np is definitely composite. If it passes, np might still
        // be composite (Carmichael numbers are the classic troublemakers).
        // But this simple test weeds out many non-prime candidates. The value
        // 228 is not a correctness requirement. Rather, it is just a performance
        // tradeoff in this interpretation of Miller-Rabin primality testing.

        const BigIntType fn{powm(BigIntType(228U), nm1, np)};

        if (!local_functor_isone(fn)) {
            return false;
        }
    }

    const unsigned k{lsb_position(static_cast<std::uint64_t>(nm1))};

    const BigIntType q{nm1 >> k};

    // Assume the test will pass, even though it usually does not pass.
    bool result_candidate_is_prime{true};

    // We will now run the trials.
    for (std::size_t trial{std::size_t{UINT8_C(0)}}; ((trial < trials) && result_candidate_is_prime); ++trial) {
        BigIntType next_rnd{};
        get_pseudo_random_integer(next_rnd, np - 2, eng);

        BigIntType y{powm(next_rnd, q, np)};

        for (auto j = std::size_t{UINT8_C(0)}; ((j < static_cast<std::size_t>(k)) && result_candidate_is_prime); ++j) {
            if (y == nm1) {
                // This trial passes and the candidate is very probably prime
                // within the limits of Miller-Rabin.

                break;
            }

            if (local_functor_isone(y)) {
                // Failure and the candidate is not prime, but only if this is
                // not the first step.

                if (j != std::size_t{UINT8_C(0)}) {
                    result_candidate_is_prime = false;
                }

                break;
            }

            // Compute y = y^2 mod np.
            y = (y * y) % np;

            // If we reach the final iteration without hitting nm1,
            // then the candidate is not prime.

            if (static_cast<unsigned>(j + std::size_t{UINT8_C(1)}) == k) {
                result_candidate_is_prime = false;
            }
        }
    }

    return result_candidate_is_prime;
}

auto main() -> int;

auto main() -> int {

    using beman::big_int::big_int;

    using candidate_rnd_gen_type = std::minstd_rand;

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY)
    candidate_rnd_gen_type rnd_eng_candidate(current_time_stamp_now<typename candidate_rnd_gen_type::result_type>());
#else
    candidate_rnd_gen_type rnd_eng_candidate(static_cast<typename candidate_rnd_gen_type::result_type>(42));
#endif // BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY

    const big_int big_max512_int{(big_int{1} << 512U) - 1};

    for (int index{0}; index < 2048; ++index) {
        static_cast<void>(index);

        using miller_rabin_rnd_gen_type = std::mt19937_64;

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY)
        miller_rabin_rnd_gen_type rnd_eng_mr{
            current_time_stamp_now<typename miller_rabin_rnd_gen_type::result_type>()};
#else
        miller_rabin_rnd_gen_type rnd_eng_mr{static_cast<typename miller_rabin_rnd_gen_type::result_type>(123)};
#endif // BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_CHRONO_ENTROPY

        big_int prime_candidate{};
        get_pseudo_random_integer(prime_candidate, big_max512_int, rnd_eng_candidate);

        const bool is_prime{miller_rabin(prime_candidate, 25, rnd_eng_mr)};

        if (is_prime) {
            const std::string str_prime{to_string(prime_candidate)};

            std::cout << "str_prime: " << str_prime << std::endl;
        }
    }
}
