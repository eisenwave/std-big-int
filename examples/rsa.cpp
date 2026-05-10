// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <array>
#include <charconv>
#include <format>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

// Define the RSA parameters. See also lines 25-30 in the
// traditional NIST CAVS file "KeyGen_186-3.rsp".

inline static constexpr char str_n[] = "d9f3094b36634c05a02ae1a5569035107a48029e39b3c6a1853817f063e18e76"
                                       "1c0c538e55ff2c7e53d603bb35cabb3b8d07f82aa0afdeaf7441fcf6746c5bca"
                                       "aa2cde398ad73edb9c340c3ffca559132581eaf8f65c13d02f3445a932a3e1fa"
                                       "db5912f7553edec5047e4d0ed06ee87effc549e194d38e06b73a971c961688ba"
                                       "2d4aa4f450d2523372f317d41d06f9f0360e962ce953a69f36c53c370799fcfb"
                                       "a195e8f691ebe862f84ae4bbd7747bc14499bd0efffcdc7154325908355c2ffc"
                                       "5b3948b8102b33aa2420381470e4ee858380ff0eea58288516c263f6d51dbbd0"
                                       "e477d1393a0a3ee60e1fde4330856665bf522006608a6104c138c0f39e09c4c5";

inline static constexpr char str_d[] = "1bf009caddc664b4404d59711fde16d7c55822449de1c5a084d22ed5791fdaa3"
                                       "7ea538867fc91a17e6856e277c2dedd70ca8bf6ec44b0e729917a88e5988cc56"
                                       "1d948ddeea46e21fd8ff46cce7657c94bfb1bdf40b3b30d4595a8bc3a15f1d4a"
                                       "d4c665c09b3b265ba19cdb0b89cbaadd0097ff52e9f6e594f86829c5bb4e9ba0"
                                       "200f12fa6dc60fd28dec0d194f08deb50f5a7749540160d6e8338e75b11165b7"
                                       "6f4650c2fcce08f979ad9941daedaa5e328473bf712f8f549c36967f5e15477d"
                                       "c643d1f48d563139134e5cdc4bb84f9782cd5125e864e067cb980290f215cb41"
                                       "090e297bac2714efba61115d85613851c2de50a82f4ab526b88c61b7c9a0b589";

inline static constexpr char str_e[] = "100000001";

inline static constexpr char str_message[] = "Hello std-big-int RSA";

template <class BigIntType>
static auto powm(BigIntType b, BigIntType p, const BigIntType& m) -> BigIntType {

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

using beman::big_int::big_int;

static auto from_hex_string(const std::string_view hex_str) -> big_int {
    big_int value_to_get{};

    const auto fc_result{from_chars(hex_str.data(), hex_str.data() + hex_str.size(), value_to_get, 16)};

    static_cast<void>(fc_result);

    return value_to_get;
}

static auto to_hex_string(big_int value_to_convert) -> std::string {
    constexpr unsigned len_per_limb{std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits / 4};

    const std::size_t buf_size{(value_to_convert.representation().size() + 1U) * len_per_limb};

    std::string result(buf_size, '\0');

    const auto [ptr, ec]{to_chars(result.data(), result.data() + result.size(), value_to_convert, 16)};

    static_cast<void>(ec);

    result.resize(static_cast<std::string::size_type>(std::distance(result.data(), ptr)));

    return result;
}

static auto ascii_to_hex(std::string_view input) -> std::string {
    constexpr std::array hex{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    std::string out{};
    out.reserve(input.size() * 2U);

    for (char c : input) {
        out.push_back(hex[static_cast<unsigned char>(c) >> 4]);
        out.push_back(hex[static_cast<unsigned char>(c) & 0x0F]);
    }

    return out;
}

static auto hex_to_ascii(std::string_view hex) -> std::string {
    std::string out{};
    out.reserve(hex.size() / 2);

    for (auto i : std::views::iota(std::size_t{0}, hex.size()) | std::views::stride(2)) {
        unsigned int value{};

        std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);

        out.push_back(static_cast<char>(value));
    }

    return out;
}

auto main() -> int {
    // Create the big_int RSA parameters from constant hex strings.
    big_int n{::from_hex_string(str_n)};
    big_int d{::from_hex_string(str_d)};
    big_int e{::from_hex_string(str_e)};

    // Convert the input ASCII text message to a hex string
    // and subsequently a big_int.
    big_int message{::from_hex_string(::ascii_to_hex(str_message))};

    // Perform RSA encryption and decryption.
    big_int cipher_text{::powm(message, e, n)};
    big_int recover{::powm(cipher_text, d, n)};

    // Represent the recovered big_int message (recover) as
    // a hex string and subsequently convert it back to its
    // ASCII representation.

    const std::string str_message_recover{::hex_to_ascii(::to_hex_string(recover))};

    std::cout << "message:\n" << str_message << std::endl;
    std::cout << "\nrecover:\n" << str_message_recover << std::endl;

    const bool result_is_ok{str_message == str_message_recover};

    std::cout << "\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
