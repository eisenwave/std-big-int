// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

// #define BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
[[nodiscard]] inline auto get_system_entropy() -> unsigned;
#endif

namespace rnd_gens {
using gen_type = std::mt19937_64;

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
auto eng1() -> gen_type& {
    static gen_type instance{get_system_entropy()};
    return instance;
};
auto eng2() -> gen_type& {
    static gen_type instance{get_system_entropy()};
    return instance;
};
#else
auto eng1() -> gen_type& {
    static gen_type instance{42};
    return instance;
};
auto eng2() -> gen_type& {
    static gen_type instance{123};
    return instance;
};
#endif
} // namespace rnd_gens

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
[[nodiscard]] inline auto get_system_entropy() -> unsigned {
    std::random_device rd{};

    return rd();
}
#endif // BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY

template <class BigIntType, class RndEngineType>
[[nodiscard]] auto get_pseudo_random_integer(RndEngineType& eng, const BigIntType& max_val) -> BigIntType {
    using distribution_type = std::uniform_int_distribution<std::uint64_t>;

    distribution_type dist{std::uint64_t{0x8000000100000001}, std::uint64_t{0xFFFFFFFFFFFFFFFF}};

    BigIntType value_to_get{};

    for (int bit_index{0}; value_to_get < max_val; bit_index += 64) {
        if (bit_index != 0U) {
            value_to_get <<= 64U;
        }
        value_to_get += dist(eng);
    }

    return value_to_get % max_val;
}

template <class BigIntType>
auto powm(BigIntType b, BigIntType p, const BigIntType& m) -> BigIntType {

    // Calculate (b ^ p) % m.

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

    return x;
}

static auto lsb_position(std::uint64_t x) noexcept -> unsigned {
    // We use tricky, enhanced knowledge here. Because of the way the prime
    // candidates are created, each limb is "rigged" to have a non-zero value.
    // So here, we can safely calculate the LSB of the big_int based on one
    // single limb.

    unsigned pos{};

    while ((static_cast<unsigned>(x) & 1U) == 0U) {
        x >>= 1U;
        ++pos;
    }

    return pos;
}

template <class BigIntType>
auto miller_rabin(const BigIntType& np, const int trials) -> bool {
    // Perform the Miller-Rabin primality test on the prime candidate np.
    // This subroutine returns true if the prime candidate is prime within
    // the limits of Miller-Rabin testing for the given input of trials.

    // All even numbers are non-prime.
    if ((static_cast<unsigned>(np) & 1U) == 0U) {
        return false;
    }

    // List of small primes.
    constexpr std::array<unsigned, std::size_t{32U}> small_primes = {
        3U,  5U,  7U,  11U, 13U, 17U, 19U, 23U, 29U,  31U,  37U,  41U,  43U,  47U,  53U,  59U,
        61U, 67U, 71U, 73U, 79U, 83U, 89U, 97U, 101U, 103U, 107U, 109U, 113U, 127U, 131U, 137U};

    const bool is_small_prime_divisible = std::any_of(
        small_primes.cbegin(), small_primes.cend(), [&np](int p) { return ((np % p) == 0U) && (np != p); });

    if (is_small_prime_divisible) {
        return false;
    }

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

    const BigIntType nm2{np - 2};

    // We will now run the trials.
    for (int trial{0}; ((trial < trials) && result_candidate_is_prime); ++trial) {
        static_cast<void>(trial);

        BigIntType next_rnd{get_pseudo_random_integer(rnd_gens::eng1(), nm2)};

        BigIntType y{powm(next_rnd, q, np)};

        for (auto j = std::size_t{UINT8_C(0)}; ((j < static_cast<std::size_t>(k)) && result_candidate_is_prime); ++j) {
            if (y == nm1) {
                // This trial passes and the candidate is very probably prime
                // within the limits of Miller-Rabin primality testing.

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
            // then the candidate is not prime within the limits of
            // Miller-Rabin primality testing.

            if (static_cast<unsigned>(j + std::size_t{UINT8_C(1)}) == k) {
                result_candidate_is_prime = false;
            }
        }
    }

    return result_candidate_is_prime;
}

[[nodiscard]] inline auto str_prime_density(std::uint64_t trial_count, std::uint64_t prime_count) -> std::string {
    std::stringstream strm{};

    strm << "prime density: 1/" << std::fixed << std::setprecision(1)
         << static_cast<float>(static_cast<double>(trial_count) / static_cast<double>(prime_count));

    return strm.str();
}

auto main() -> int {

    using beman::big_int::big_int;

    const big_int max_val{(big_int{1} << 512U) - 1};

    std::uint64_t prime_count{};
    std::uint64_t trial_count{};

    constexpr std::uint64_t max_prime_count{32};
    // constexpr std::uint64_t max_prime_count{250000};

    while (prime_count < max_prime_count) {

#if defined(BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY)
        rnd_gens::eng1().seed(static_cast<typename rnd_gens::gen_type::result_type>(get_system_entropy()));
        rnd_gens::eng2().seed(static_cast<typename rnd_gens::gen_type::result_type>(get_system_entropy()));
#endif

        big_int prime_candidate{get_pseudo_random_integer(rnd_gens::eng2(), max_val)};
        ++trial_count;

        const bool is_prime{miller_rabin(prime_candidate, 25)};

        if (is_prime) {
            const std::string str_prime{to_string(prime_candidate)};

            ++prime_count;

            std::cout << "prime" << prime_count << "/" << trial_count << ", "
                      << str_prime_density(trial_count, prime_count) << ": " << str_prime << std::endl;
        }
    }

    std::cout << str_prime_density(trial_count, prime_count) << std::endl;
}
