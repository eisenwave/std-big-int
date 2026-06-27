// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/charconv.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace local {

// Forward declaration of ring_allocator_base.
template <const std::uint_fast32_t buffer_size>
class ring_allocator_base;

// Global comparison operators (required by the standard).
template <const std::uint_fast32_t buffer_size>
auto operator==(const ring_allocator_base<buffer_size>&, const ring_allocator_base<buffer_size>&) noexcept -> bool;

template <const std::uint_fast32_t buffer_size>
auto operator!=(const ring_allocator_base<buffer_size>&, const ring_allocator_base<buffer_size>&) noexcept -> bool;

template <const std::uint_fast32_t buffer_size>
class ring_allocator_base {
  private:
    static constexpr std::uint_fast8_t buffer_alignment{UINT8_C(16)};

  public:
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    virtual ~ring_allocator_base() = default;

  protected:
    static constexpr std::uint_fast8_t bufffer_alignment{UINT8_C(16)};

    ring_allocator_base() noexcept = default;

    ring_allocator_base(const ring_allocator_base&) noexcept = default;

    ring_allocator_base(ring_allocator_base&&) noexcept = default;

    auto operator=(const ring_allocator_base&) noexcept -> ring_allocator_base& = default;
    auto operator=(ring_allocator_base&&) noexcept -> ring_allocator_base&      = default;

    // The ring allocator's buffer type.
    struct buffer_type {
        static constexpr size_type local_buf_size{static_cast<size_type>(buffer_size)};

        std::array<std::uint8_t, local_buf_size> arena;

        buffer_type() noexcept {}
    };

    // The ring allocator's memory allocation.
    static auto do_allocate(size_type chunk_size) -> void* {
        alignas(bufffer_alignment) static buffer_type buffer{};

        static std::uint8_t* get_ptr{buffer.arena.data()};

        // Get the newly allocated pointer.
        std::uint8_t* p{get_ptr};

        // Increment the get-pointer for the next allocation.
        // Be sure to handle the buffer alignment.

        const std::uint_fast8_t misaligned_amount(chunk_size % buffer_alignment);

        if (misaligned_amount != UINT8_C(0)) {
            chunk_size += size_type(buffer_alignment - misaligned_amount);
        }

        get_ptr += chunk_size;

        // Does this attempted allocation overflow the capacity of the buffer?
        const bool is_overflow{(get_ptr >= (buffer.arena.data() + buffer_type::local_buf_size))};

        if (is_overflow) {
            // The buffer has overflowed.

#if (defined(__GNUC__) && !defined(__clang__))
    #if (__GNUC__ >= 12)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Warray-bounds"
    #endif
#endif

            // Reset the allocated pointer to the bottom of the buffer
            // and increment the next get-pointer.
            p       = &buffer.arena[std::size_t{UINT8_C(0)}];
            get_ptr = &buffer.arena[chunk_size];

#if (defined(__GNUC__) && !defined(__clang__))
    #if (__GNUC__ >= 12)
        #pragma GCC diagnostic pop
    #endif
#endif
        }

        return static_cast<void*>(p);
    }

    // Global comparison operators (required by the standard).
    friend auto operator==(const ring_allocator_base&, const ring_allocator_base&) noexcept -> bool { return true; }
    friend auto operator!=(const ring_allocator_base&, const ring_allocator_base&) noexcept -> bool { return false; }
};

template <typename T, const std::uint_fast32_t buffer_size>
class ring_allocator;

template <const std::uint_fast32_t buffer_size>
class ring_allocator<void, buffer_size> : public ring_allocator_base<buffer_size> {
  public:
    using value_type    = void;
    using pointer       = value_type*;
    using const_pointer = const value_type*;

    template <typename U>
    struct rebind {
        using other = ring_allocator<U, buffer_size>;
    };
};

template <typename T, const std::uint_fast32_t buffer_size>
class ring_allocator : public ring_allocator_base<buffer_size> {
  public:
    static_assert(sizeof(T) <= ring_allocator_base<buffer_size>::buffer_type::local_buf_size,
                  "The size of the allocation object can not exceed the buffer size.");

    using value_type      = T;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;
    using reference       = value_type&;
    using const_reference = const value_type&;

    ring_allocator() noexcept = default;

    ring_allocator(const ring_allocator&) noexcept : ring_allocator_base<buffer_size>(ring_allocator()) {}
    ring_allocator(ring_allocator&&) noexcept : ring_allocator_base<buffer_size>(std::move(ring_allocator())) {}

    auto operator=(const ring_allocator&) noexcept -> ring_allocator& = default;
    auto operator=(ring_allocator&&) noexcept -> ring_allocator&      = default;

    template <typename U>
    ring_allocator(const ring_allocator<U, buffer_size>&) noexcept {}

    ~ring_allocator() override = default;

    template <typename U>
    struct rebind {
        using other = ring_allocator<U, buffer_size>;
    };

    auto max_size() const noexcept -> typename ring_allocator<void, buffer_size>::size_type {
        return ring_allocator_base<buffer_size>::buffer_type::size / sizeof(value_type);
    }

    auto address(reference x) const -> pointer { return &x; }
    auto address(const_reference x) const -> const_pointer { return &x; }

    auto allocate(typename ring_allocator<void, buffer_size>::size_type count,
                  typename ring_allocator<void, buffer_size>::const_pointer = nullptr) -> pointer {
        const typename ring_allocator<void, buffer_size>::size_type chunk_size = count * sizeof(value_type);

        void* p = ring_allocator<void, buffer_size>::do_allocate(chunk_size);

        return static_cast<pointer>(p);
    }

    auto construct(pointer p, const value_type& x) noexcept -> void { new (static_cast<void*>(p)) value_type(x); }

    auto destroy(pointer p) noexcept -> void { p->~value_type(); }

    auto deallocate(pointer, typename ring_allocator<void, buffer_size>::size_type) noexcept -> void {}
};

} // namespace local

static auto do_one_test() -> bool;

static auto do_one_test() -> bool {
    using allocator_type = local::ring_allocator<beman::big_int::uint_multiprecision_t, std::uint_fast32_t{0x2000U}>;

    using wi_type = beman::big_int::basic_big_int<std::numeric_limits<typename allocator_type::value_type>::digits,
                                                  allocator_type>;

    using wi_result_type = wi_type;

    constexpr char pstr_wi_val_a[] =
        "fee6f3060ed3f90fdd79fe414418f8d9dc08bbe4470b658ca8f167fc3ce48821a79f8f9df51d795cbb88cb6e3a5e5f46b56f06991d6a9"
        "29784b414c0bff17ca7eef9ab0e4d469093c548018d66a349beda36a4afdeb9d329d4119e93fa436a2b8417c3b2af701dde827e01e608"
        "c3ddfedfdc7fd7052fda87efb34d8321f3941482bc74e109ebdff9aebc9585de04ab47afb41ccbda18d806eb3d87ed7b1c0f03954ef98"
        "f08432db8f86e4ebceb292c53ac83f9738f8cd88da17384cdfb31d25bf6e030571d52ac43a7c646dee1fc0a8d827d73917b3c5f84dc7d"
        "1515a0c9a1bef3dc7f9d8caed3b3db7c869e262860a4e008c12d5f3da2c733c0f55168a30be2";
    constexpr char pstr_wi_val_b[] =
        "d7f1c5ebfe9108ab8900f3d1af36367cce3e92121acb9b60e352e5d622525715d0203ddd77d9ab308709777b225948c3e61542010fe75"
        "974ddb38cc38a11fa65bee2a6f171fe7ee4e52d81569bfcb886f972bd4655b6388bc7c8982f6e6a31efc21f8579394b5e629c15c367ee"
        "6eda6091dcb0f8aea6daa69f9c7059e585fc92a28c16d16183b8b0edf460b9741b712a9aaaf9d557ae4a7ef5cead986e9c414988b5fc4"
        "3d78d8f7fbbc7f6cfd7e08bf8f87ce6f865b9ad1fcf4b00f42939349398c4a2928c21959ff6105665de3d95be8afc9fe9fb33d6afb959"
        "48e9778328fdd7172c70431479229cae5a47463f832b9ca2a39f36963f3ec1dc39fe6a3bffbc";
    constexpr char pstr_wi_val_ctrl[] =
        "d704b29793a64a44339b92e4f202801cae2ca252f899cf36ae3735c98505f4c16a7a6a159e54e8927d4ec95defd8ec04afcc0043bdab5"
        "0dfc763b376549ab90df60d476bd5b26c3f5b2378656fe307f7da459b62924efa9f6d408484a44a88265515b04f1d75351ba7e6436cdf"
        "6d67a87c82dbacb02f782f1601feb974d10367e275561904466d31daf817f350752636455bf7f6f86f21268bf173182e430fdeb80fbc9"
        "6daf032ec381f761476b8bf2cc33d6ec7d67d96eb69fef2bce4c715e4d676b8c2066afa83f10243273f5c65b427e71eaee89d285f3969"
        "2e757cb6b86a085e7869ef85c05155328130e37163b8addb3410ccd9fde92fcc5e8a9b3336113440f34e3ace928c98de63d02c0caa1c1"
        "60cfb8ccf46d48bdf9ebd24f5611582d92cf787e02198e7bebc83bd9eae8bd63c5b74573a2923146d05e92211baecb7fb4d7408d31cdc"
        "cdfdeca05c0c4085775a510f4bed924ebd69e6c4e34c3c6ecb3de0ff2187affe6bffaa1d73df44d730668318922a3077232dab4575a64"
        "e8a7958301ce2e341a146f758fc51d7e25298545a6b277b1a9cd9ea72def76d7c05ea5a17ba501050106ff8042f0ffea4c97193b0b8ad"
        "138e315d7cd09179d55782534c96b32f21d808e0764b3a341ccc1c20543da2ef9620a5c877fa330bfc43556ddf8069c3fa04e3081b699"
        "b8673346f82d112c49bf17fa7c52cc9fbbe91a8d7f8";

    wi_type        wi_val_a{};
    wi_type        wi_val_b{};
    wi_result_type wi_val_ctrl{};

    static_cast<void>(from_chars(pstr_wi_val_a, pstr_wi_val_a + sizeof(pstr_wi_val_a), wi_val_a, 16));
    static_cast<void>(from_chars(pstr_wi_val_b, pstr_wi_val_b + sizeof(pstr_wi_val_a), wi_val_b, 16));
    static_cast<void>(from_chars(pstr_wi_val_ctrl, pstr_wi_val_ctrl + sizeof(pstr_wi_val_ctrl), wi_val_ctrl, 16));

    const wi_result_type wi_val_c{wi_val_a * wi_val_b};

    const bool result_is_ok{(wi_val_ctrl == wi_val_c)};

    return result_is_ok;
}

namespace app::benchmark {
auto run_std_big_int() -> bool;
}

auto app::benchmark::run_std_big_int() -> bool {
    const bool result_is_ok{::do_one_test()};

    return result_is_ok;
}

#if defined(APP_BENCHMARK_STANDALONE_MAIN)
constexpr auto app_benchmark_standalone_foodcafe = static_cast<std::uint32_t>(UINT32_C(0xF00DCAFE));

extern "C" {
extern volatile std::uint32_t app_benchmark_standalone_result;

auto app_benchmark_run_standalone() -> bool;
auto app_benchmark_get_standalone_result() -> bool;

auto app_benchmark_run_standalone() -> bool {
    auto result_is_ok = true;

    for (unsigned i = 0U; i < 64U; ++i) {
        result_is_ok &= app::benchmark::run_std_big_int();
    }

    app_benchmark_standalone_result = static_cast<std::uint32_t>(
        result_is_ok ? app_benchmark_standalone_foodcafe : static_cast<std::uint32_t>(UINT32_C(0xFFFFFFFF)));

    return result_is_ok;
}

auto app_benchmark_get_standalone_result() -> bool {
    volatile auto result_is_ok = (app_benchmark_standalone_result == static_cast<std::uint32_t>(UINT32_C(0xF00DCAFE)));

    return result_is_ok;
}
}

__attribute__((used)) auto main() -> int;

auto main() -> int {
    auto result_is_ok = true;

    result_is_ok = (::app_benchmark_run_standalone() && result_is_ok);
    result_is_ok = (::app_benchmark_get_standalone_result() && result_is_ok);

    return (result_is_ok ? 0 : -1);
}

extern "C" {
volatile std::uint32_t app_benchmark_standalone_result;
}
#endif
