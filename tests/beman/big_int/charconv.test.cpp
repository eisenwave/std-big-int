#include <string_view>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

namespace {

using namespace beman::big_int;

static_assert(std::has_single_bit(detail::width_v<uint_multiprecision_t>),
              "The to_chars and from_chars implementations assume "
              "that the limb width is a power of two.");

[[nodiscard]] constexpr big_int parse(const std::string_view str, const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2 && base <= 36);
    big_int result;
    const auto [p, ec] = from_chars(str.data(), str.data() + str.size(), result, base);
    if (ec != std::errc{}) {
        throw std::runtime_error("from_chars did not succeed.");
    }
    if (p != str.data() + str.size()) {
        throw std::runtime_error("from_chars parsed only part of the string.");
    }
    return result;
}

// clang-format off
static_assert(1234567890123456789012345678901234567890112233445566778899_n == parse("1100100101100101111101011111010010101011010111010110000000010101011010001111000011110010000101101011001110110000010011101110011110001010001000010010010011111010010110111101010000111000010011", 2),
              "The tests in this file are meaningless because parsing of literals is broken.");
// Powers of two are handled differently, so a second check makes sense.
static_assert(1234567890123456789012345678901234567890112233445566778899_n == parse("2443030312003032423221132100412422141210014133224140342114014311130233010043411044", 5));

TEST(Charconv, LimbMaxInputDigits) {
    if constexpr (detail::width_v<uint_multiprecision_t> == 64) {
        EXPECT_EQ(detail::limb_max_input_digits(2), 64);
        EXPECT_EQ(detail::limb_max_input_digits(3), 40);
        EXPECT_EQ(detail::limb_max_input_digits(4), 32);
        EXPECT_EQ(detail::limb_max_input_digits(5), 27);
        EXPECT_EQ(detail::limb_max_input_digits(6), 24);
        EXPECT_EQ(detail::limb_max_input_digits(7), 22);
        EXPECT_EQ(detail::limb_max_input_digits(8), 21);
        EXPECT_EQ(detail::limb_max_input_digits(9), 20);
        EXPECT_EQ(detail::limb_max_input_digits(10), 19);
        EXPECT_EQ(detail::limb_max_input_digits(11), 18);
        EXPECT_EQ(detail::limb_max_input_digits(12), 17);
        EXPECT_EQ(detail::limb_max_input_digits(13), 17);
        EXPECT_EQ(detail::limb_max_input_digits(14), 16);
        EXPECT_EQ(detail::limb_max_input_digits(15), 16);
        EXPECT_EQ(detail::limb_max_input_digits(16), 16);
        EXPECT_EQ(detail::limb_max_input_digits(17), 15);
        EXPECT_EQ(detail::limb_max_input_digits(18), 15);
        EXPECT_EQ(detail::limb_max_input_digits(19), 15);
        EXPECT_EQ(detail::limb_max_input_digits(20), 14);
        EXPECT_EQ(detail::limb_max_input_digits(21), 14);
        EXPECT_EQ(detail::limb_max_input_digits(22), 14);
        EXPECT_EQ(detail::limb_max_input_digits(23), 14);
        EXPECT_EQ(detail::limb_max_input_digits(24), 13);
        EXPECT_EQ(detail::limb_max_input_digits(25), 13);
        EXPECT_EQ(detail::limb_max_input_digits(26), 13);
        EXPECT_EQ(detail::limb_max_input_digits(27), 13);
        EXPECT_EQ(detail::limb_max_input_digits(28), 13);
        EXPECT_EQ(detail::limb_max_input_digits(29), 13);
        EXPECT_EQ(detail::limb_max_input_digits(30), 13);
        EXPECT_EQ(detail::limb_max_input_digits(31), 12);
        EXPECT_EQ(detail::limb_max_input_digits(32), 12);
        EXPECT_EQ(detail::limb_max_input_digits(33), 12);
        EXPECT_EQ(detail::limb_max_input_digits(34), 12);
        EXPECT_EQ(detail::limb_max_input_digits(35), 12);
        EXPECT_EQ(detail::limb_max_input_digits(36), 12);
    }
}

TEST(Charconv, LimbMaxPower) {
    if constexpr (detail::width_v<uint_multiprecision_t> == 64) {
        EXPECT_EQ(detail::limb_max_power(2), 0ull);
        EXPECT_EQ(detail::limb_max_power(3), 12157665459056928801ull);
        EXPECT_EQ(detail::limb_max_power(4), 0ull);
        EXPECT_EQ(detail::limb_max_power(5), 7450580596923828125ull);
        EXPECT_EQ(detail::limb_max_power(6), 4738381338321616896ull);
        EXPECT_EQ(detail::limb_max_power(7), 3909821048582988049ull);
        EXPECT_EQ(detail::limb_max_power(8), 9223372036854775808ull);
        EXPECT_EQ(detail::limb_max_power(9), 12157665459056928801ull);
        EXPECT_EQ(detail::limb_max_power(10), 10000000000000000000ull);
        EXPECT_EQ(detail::limb_max_power(11), 5559917313492231481ull);
        EXPECT_EQ(detail::limb_max_power(12), 2218611106740436992ull);
        EXPECT_EQ(detail::limb_max_power(13), 8650415919381337933ull);
        EXPECT_EQ(detail::limb_max_power(14), 2177953337809371136ull);
        EXPECT_EQ(detail::limb_max_power(15), 6568408355712890625ull);
        EXPECT_EQ(detail::limb_max_power(16), 0ull);
        EXPECT_EQ(detail::limb_max_power(17), 2862423051509815793ull);
        EXPECT_EQ(detail::limb_max_power(18), 6746640616477458432ull);
        EXPECT_EQ(detail::limb_max_power(19), 15181127029874798299ull);
        EXPECT_EQ(detail::limb_max_power(20), 1638400000000000000ull);
        EXPECT_EQ(detail::limb_max_power(21), 3243919932521508681ull);
        EXPECT_EQ(detail::limb_max_power(22), 6221821273427820544ull);
        EXPECT_EQ(detail::limb_max_power(23), 11592836324538749809ull);
        EXPECT_EQ(detail::limb_max_power(24), 876488338465357824ull);
        EXPECT_EQ(detail::limb_max_power(25), 1490116119384765625ull);
        EXPECT_EQ(detail::limb_max_power(26), 2481152873203736576ull);
        EXPECT_EQ(detail::limb_max_power(27), 4052555153018976267ull);
        EXPECT_EQ(detail::limb_max_power(28), 6502111422497947648ull);
        EXPECT_EQ(detail::limb_max_power(29), 10260628712958602189ull);
        EXPECT_EQ(detail::limb_max_power(30), 15943230000000000000ull);
        EXPECT_EQ(detail::limb_max_power(31), 787662783788549761ull);
        EXPECT_EQ(detail::limb_max_power(32), 1152921504606846976ull);
        EXPECT_EQ(detail::limb_max_power(33), 1667889514952984961ull);
        EXPECT_EQ(detail::limb_max_power(34), 2386420683693101056ull);
        EXPECT_EQ(detail::limb_max_power(35), 3379220508056640625ull);
        EXPECT_EQ(detail::limb_max_power(36), 4738381338321616896ull);
    }
}

TEST(FromChars, DigitValue) {
    EXPECT_EQ(detail::digit_value('0'), 0);
    EXPECT_EQ(detail::digit_value('1'), 1);
    EXPECT_EQ(detail::digit_value('2'), 2);
    EXPECT_EQ(detail::digit_value('3'), 3);
    EXPECT_EQ(detail::digit_value('4'), 4);
    EXPECT_EQ(detail::digit_value('5'), 5);
    EXPECT_EQ(detail::digit_value('6'), 6);
    EXPECT_EQ(detail::digit_value('7'), 7);
    EXPECT_EQ(detail::digit_value('8'), 8);
    EXPECT_EQ(detail::digit_value('9'), 9);

    EXPECT_EQ(detail::digit_value('a'), 10);
    EXPECT_EQ(detail::digit_value('b'), 11);
    EXPECT_EQ(detail::digit_value('c'), 12);
    EXPECT_EQ(detail::digit_value('d'), 13);
    EXPECT_EQ(detail::digit_value('e'), 14);
    EXPECT_EQ(detail::digit_value('f'), 15);
    EXPECT_EQ(detail::digit_value('g'), 16);
    EXPECT_EQ(detail::digit_value('h'), 17);
    EXPECT_EQ(detail::digit_value('i'), 18);
    EXPECT_EQ(detail::digit_value('j'), 19);
    EXPECT_EQ(detail::digit_value('k'), 20);
    EXPECT_EQ(detail::digit_value('l'), 21);
    EXPECT_EQ(detail::digit_value('m'), 22);
    EXPECT_EQ(detail::digit_value('n'), 23);
    EXPECT_EQ(detail::digit_value('o'), 24);
    EXPECT_EQ(detail::digit_value('p'), 25);
    EXPECT_EQ(detail::digit_value('q'), 26);
    EXPECT_EQ(detail::digit_value('r'), 27);
    EXPECT_EQ(detail::digit_value('s'), 28);
    EXPECT_EQ(detail::digit_value('t'), 29);
    EXPECT_EQ(detail::digit_value('u'), 30);
    EXPECT_EQ(detail::digit_value('v'), 31);
    EXPECT_EQ(detail::digit_value('w'), 32);
    EXPECT_EQ(detail::digit_value('x'), 33);
    EXPECT_EQ(detail::digit_value('y'), 34);
    EXPECT_EQ(detail::digit_value('z'), 35);

    EXPECT_EQ(detail::digit_value('A'), 10);
    EXPECT_EQ(detail::digit_value('B'), 11);
    EXPECT_EQ(detail::digit_value('C'), 12);
    EXPECT_EQ(detail::digit_value('D'), 13);
    EXPECT_EQ(detail::digit_value('E'), 14);
    EXPECT_EQ(detail::digit_value('F'), 15);
    EXPECT_EQ(detail::digit_value('G'), 16);
    EXPECT_EQ(detail::digit_value('H'), 17);
    EXPECT_EQ(detail::digit_value('I'), 18);
    EXPECT_EQ(detail::digit_value('J'), 19);
    EXPECT_EQ(detail::digit_value('K'), 20);
    EXPECT_EQ(detail::digit_value('L'), 21);
    EXPECT_EQ(detail::digit_value('M'), 22);
    EXPECT_EQ(detail::digit_value('N'), 23);
    EXPECT_EQ(detail::digit_value('O'), 24);
    EXPECT_EQ(detail::digit_value('P'), 25);
    EXPECT_EQ(detail::digit_value('Q'), 26);
    EXPECT_EQ(detail::digit_value('R'), 27);
    EXPECT_EQ(detail::digit_value('S'), 28);
    EXPECT_EQ(detail::digit_value('T'), 29);
    EXPECT_EQ(detail::digit_value('U'), 30);
    EXPECT_EQ(detail::digit_value('V'), 31);
    EXPECT_EQ(detail::digit_value('W'), 32);
    EXPECT_EQ(detail::digit_value('X'), 33);
    EXPECT_EQ(detail::digit_value('Y'), 34);
    EXPECT_EQ(detail::digit_value('Z'), 35);

    constexpr std::string_view nondigit_basic_characters = "\t\f\n !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    for (const char c : nondigit_basic_characters) {
        EXPECT_EQ(detail::digit_value(c), -1);
    }
}

TEST(FromChars, EveryBase_255) {
    constexpr big_int expected = 255;
    EXPECT_EQ(parse("11111111", 2), expected);
    EXPECT_EQ(parse("100110", 3), expected);
    EXPECT_EQ(parse("3333", 4), expected);
    EXPECT_EQ(parse("2010", 5), expected);
    EXPECT_EQ(parse("1103", 6), expected);
    EXPECT_EQ(parse("513", 7), expected);
    EXPECT_EQ(parse("377", 8), expected);
    EXPECT_EQ(parse("313", 9), expected);
    EXPECT_EQ(parse("255", 10), expected);
    EXPECT_EQ(parse("212", 11), expected);
    EXPECT_EQ(parse("193", 12), expected);
    EXPECT_EQ(parse("168", 13), expected);
    EXPECT_EQ(parse("143", 14), expected);
    EXPECT_EQ(parse("120", 15), expected);
    EXPECT_EQ(parse("ff", 16), expected);
    EXPECT_EQ(parse("f0", 17), expected);
    EXPECT_EQ(parse("e3", 18), expected);
    EXPECT_EQ(parse("d8", 19), expected);
    EXPECT_EQ(parse("cf", 20), expected);
    EXPECT_EQ(parse("c3", 21), expected);
    EXPECT_EQ(parse("bd", 22), expected);
    EXPECT_EQ(parse("b2", 23), expected);
    EXPECT_EQ(parse("af", 24), expected);
    EXPECT_EQ(parse("a5", 25), expected);
    EXPECT_EQ(parse("9l", 26), expected);
    EXPECT_EQ(parse("9c", 27), expected);
    EXPECT_EQ(parse("93", 28), expected);
    EXPECT_EQ(parse("8n", 29), expected);
    EXPECT_EQ(parse("8f", 30), expected);
    EXPECT_EQ(parse("87", 31), expected);
    EXPECT_EQ(parse("7v", 32), expected);
    EXPECT_EQ(parse("7o", 33), expected);
    EXPECT_EQ(parse("7h", 34), expected);
    EXPECT_EQ(parse("7a", 35), expected);
    EXPECT_EQ(parse("73", 36), expected);
}

TEST(FromChars, EveryBase_m255) {
    constexpr big_int expected = -255;
    EXPECT_EQ(parse("-11111111", 2), expected);
    EXPECT_EQ(parse("-100110", 3), expected);
    EXPECT_EQ(parse("-3333", 4), expected);
    EXPECT_EQ(parse("-2010", 5), expected);
    EXPECT_EQ(parse("-1103", 6), expected);
    EXPECT_EQ(parse("-513", 7), expected);
    EXPECT_EQ(parse("-377", 8), expected);
    EXPECT_EQ(parse("-313", 9), expected);
    EXPECT_EQ(parse("-255", 10), expected);
    EXPECT_EQ(parse("-212", 11), expected);
    EXPECT_EQ(parse("-193", 12), expected);
    EXPECT_EQ(parse("-168", 13), expected);
    EXPECT_EQ(parse("-143", 14), expected);
    EXPECT_EQ(parse("-120", 15), expected);
    EXPECT_EQ(parse("-ff", 16), expected);
    EXPECT_EQ(parse("-f0", 17), expected);
    EXPECT_EQ(parse("-e3", 18), expected);
    EXPECT_EQ(parse("-d8", 19), expected);
    EXPECT_EQ(parse("-cf", 20), expected);
    EXPECT_EQ(parse("-c3", 21), expected);
    EXPECT_EQ(parse("-bd", 22), expected);
    EXPECT_EQ(parse("-b2", 23), expected);
    EXPECT_EQ(parse("-af", 24), expected);
    EXPECT_EQ(parse("-a5", 25), expected);
    EXPECT_EQ(parse("-9l", 26), expected);
    EXPECT_EQ(parse("-9c", 27), expected);
    EXPECT_EQ(parse("-93", 28), expected);
    EXPECT_EQ(parse("-8n", 29), expected);
    EXPECT_EQ(parse("-8f", 30), expected);
    EXPECT_EQ(parse("-87", 31), expected);
    EXPECT_EQ(parse("-7v", 32), expected);
    EXPECT_EQ(parse("-7o", 33), expected);
    EXPECT_EQ(parse("-7h", 34), expected);
    EXPECT_EQ(parse("-7a", 35), expected);
    EXPECT_EQ(parse("-73", 36), expected);
}

TEST(FromChars, EveryBase_0x1p64) {
    const big_int expected = 1_n << 64;
    EXPECT_EQ(parse("10000000000000000000000000000000000000000000000000000000000000000", 2), expected);
    EXPECT_EQ(parse("11112220022122120101211020120210210211221", 3), expected);
    EXPECT_EQ(parse("100000000000000000000000000000000", 4), expected);
    EXPECT_EQ(parse("2214220303114400424121122431", 5), expected);
    EXPECT_EQ(parse("3520522010102100444244424", 6), expected);
    EXPECT_EQ(parse("45012021522523134134602", 7), expected);
    EXPECT_EQ(parse("2000000000000000000000", 8), expected);
    EXPECT_EQ(parse("145808576354216723757", 9), expected);
    EXPECT_EQ(parse("18446744073709551616", 10), expected);
    EXPECT_EQ(parse("335500516a429071285", 11), expected);
    EXPECT_EQ(parse("839365134a2a240714", 12), expected);
    EXPECT_EQ(parse("219505a9511a867b73", 13), expected);
    EXPECT_EQ(parse("8681049adb03db172", 14), expected);
    EXPECT_EQ(parse("2c1d56b648c6cd111", 15), expected);
    EXPECT_EQ(parse("10000000000000000", 16), expected);
    EXPECT_EQ(parse("67979g60f5428011", 17), expected);
    EXPECT_EQ(parse("2d3fgb0b9cg4bd2g", 18), expected);
    EXPECT_EQ(parse("141c8786h1ccaagh", 19), expected);
    EXPECT_EQ(parse("b53bjh07be4dj0g", 20), expected);
    EXPECT_EQ(parse("5e8g4ggg7g56dig", 21), expected);
    EXPECT_EQ(parse("2l4lf104353j8kg", 22), expected);
    EXPECT_EQ(parse("1ddh88h2782i516", 23), expected);
    EXPECT_EQ(parse("l12ee5fn0ji1ig", 24), expected);
    EXPECT_EQ(parse("c9c336o0mlb7eg", 25), expected);
    EXPECT_EQ(parse("7b7n2pcniokcgg", 26), expected);
    EXPECT_EQ(parse("4eo8hfam6fllmp", 27), expected);
    EXPECT_EQ(parse("2nc6j26l66rhog", 28), expected);
    EXPECT_EQ(parse("1n3rsh11f098ro", 29), expected);
    EXPECT_EQ(parse("14l9lkmo30o40g", 30), expected);
    EXPECT_EQ(parse("nd075ib45k86g", 31), expected);
    EXPECT_EQ(parse("g000000000000", 32), expected);
    EXPECT_EQ(parse("b1w8p7j5q9r6g", 33), expected);
    EXPECT_EQ(parse("7orp63sh4dphi", 34), expected);
    EXPECT_EQ(parse("5g24a25twkwfg", 35), expected);
    EXPECT_EQ(parse("3w5e11264sgsg", 36), expected);
}

TEST(FromChars, EveryBase_m0x1p64) {
    const big_int expected = -1_n << 64;
    EXPECT_EQ(parse("-10000000000000000000000000000000000000000000000000000000000000000", 2), expected);
    EXPECT_EQ(parse("-11112220022122120101211020120210210211221", 3), expected);
    EXPECT_EQ(parse("-100000000000000000000000000000000", 4), expected);
    EXPECT_EQ(parse("-2214220303114400424121122431", 5), expected);
    EXPECT_EQ(parse("-3520522010102100444244424", 6), expected);
    EXPECT_EQ(parse("-45012021522523134134602", 7), expected);
    EXPECT_EQ(parse("-2000000000000000000000", 8), expected);
    EXPECT_EQ(parse("-145808576354216723757", 9), expected);
    EXPECT_EQ(parse("-18446744073709551616", 10), expected);
    EXPECT_EQ(parse("-335500516a429071285", 11), expected);
    EXPECT_EQ(parse("-839365134a2a240714", 12), expected);
    EXPECT_EQ(parse("-219505a9511a867b73", 13), expected);
    EXPECT_EQ(parse("-8681049adb03db172", 14), expected);
    EXPECT_EQ(parse("-2c1d56b648c6cd111", 15), expected);
    EXPECT_EQ(parse("-10000000000000000", 16), expected);
    EXPECT_EQ(parse("-67979g60f5428011", 17), expected);
    EXPECT_EQ(parse("-2d3fgb0b9cg4bd2g", 18), expected);
    EXPECT_EQ(parse("-141c8786h1ccaagh", 19), expected);
    EXPECT_EQ(parse("-b53bjh07be4dj0g", 20), expected);
    EXPECT_EQ(parse("-5e8g4ggg7g56dig", 21), expected);
    EXPECT_EQ(parse("-2l4lf104353j8kg", 22), expected);
    EXPECT_EQ(parse("-1ddh88h2782i516", 23), expected);
    EXPECT_EQ(parse("-l12ee5fn0ji1ig", 24), expected);
    EXPECT_EQ(parse("-c9c336o0mlb7eg", 25), expected);
    EXPECT_EQ(parse("-7b7n2pcniokcgg", 26), expected);
    EXPECT_EQ(parse("-4eo8hfam6fllmp", 27), expected);
    EXPECT_EQ(parse("-2nc6j26l66rhog", 28), expected);
    EXPECT_EQ(parse("-1n3rsh11f098ro", 29), expected);
    EXPECT_EQ(parse("-14l9lkmo30o40g", 30), expected);
    EXPECT_EQ(parse("-nd075ib45k86g", 31), expected);
    EXPECT_EQ(parse("-g000000000000", 32), expected);
    EXPECT_EQ(parse("-b1w8p7j5q9r6g", 33), expected);
    EXPECT_EQ(parse("-7orp63sh4dphi", 34), expected);
    EXPECT_EQ(parse("-5g24a25twkwfg", 35), expected);
    EXPECT_EQ(parse("-3w5e11264sgsg", 36), expected);
}

TEST(FromChars, EveryBase_0x1p127m1) {
    const big_int expected = (1_n << 127) - 1;
    EXPECT_EQ(parse("1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111", 2), expected);
    EXPECT_EQ(parse("101100201022001010121000102002120122110122221010202000122201220121120010200022001", 3), expected);
    EXPECT_EQ(parse("1333333333333333333333333333333333333333333333333333333333333333", 4), expected);
    EXPECT_EQ(parse("3013030220323124042102424341431241221233040112312340402", 5), expected);
    EXPECT_EQ(parse("11324454543055553250455021551551121442554522203131", 6), expected);
    EXPECT_EQ(parse("1406241064412313155000336513424310163013142501", 7), expected);
    EXPECT_EQ(parse("1777777777777777777777777777777777777777777", 8), expected);
    EXPECT_EQ(parse("11321261117012076573587122018656546120261", 9), expected);
    EXPECT_EQ(parse("170141183460469231731687303715884105727", 10), expected);
    EXPECT_EQ(parse("555a8020989a11327710815513a946a188726", 11), expected);
    EXPECT_EQ(parse("2a695925806818735399a37a20a31b3534a7", 12), expected);
    EXPECT_EQ(parse("2373464c8a3cb25ba2b7c6382b2963bb71a", 13), expected);
    EXPECT_EQ(parse("27c22d5b9734a1517bb1dc612904a79d71", 14), expected);
    EXPECT_EQ(parse("3e2480b3404d8bb9bca3084369ba3e187", 15), expected);
    EXPECT_EQ(parse("7fffffffffffffffffffffffffffffff", 16), expected);
    EXPECT_EQ(parse("13d03cge4242f3e39f9dga60476a8098", 17), expected);
    EXPECT_EQ(parse("3d51ddf66g5befc8e19d2607hc26e31", 18), expected);
    EXPECT_EQ(parse("e09c09h6a4eihac8fchc875gf4di41", 19), expected);
    EXPECT_EQ(parse("337d04g0ec2d918ac3j85180dfd467", 20), expected);
    EXPECT_EQ(parse("g3b663ge01jk6cica417i3a75c601", 21), expected);
    EXPECT_EQ(parse("48f5dj8i8eli87ecigb8g6egjhchh", 22), expected);
    EXPECT_EQ(parse("162g6gam6d49ik37jk2mdcl41aj6h", 23), expected);
    EXPECT_EQ(parse("95b794mjicl2m0cbfjnjnd2hdm57", 24), expected);
    EXPECT_EQ(parse("31ffc3d7km5eej9ge7bdfk6d7j42", 25), expected);
    EXPECT_EQ(parse("11gf2k68m8of9j9agmk61a6g4i0n", 26), expected);
    EXPECT_EQ(parse("a9j813g0b2fhchp3k0hjogf3i81", 27), expected);
    EXPECT_EQ(parse("40j3ek89l5h69q3q6jh2khn6dof", 28), expected);
    EXPECT_EQ(parse("1hp3d3elmjg86isj584dklf2djq", 29), expected);
    EXPECT_EQ(parse("k2chs8jacc5g718abkqgokq447", 30), expected);
    EXPECT_EQ(parse("8q7cd4uoh31nng3aqs4edr0p73", 31), expected);
    EXPECT_EQ(parse("3vvvvvvvvvvvvvvvvvvvvvvvvv", 32), expected);
    EXPECT_EQ(parse("1s5acer890k5h07uh412q2mo0s", 33), expected);
    EXPECT_EQ(parse("ttq3btpo06a5neskugckddplp", 34), expected);
    EXPECT_EQ(parse("evh2xn5qudpcldl13shtyuixm", 35), expected);
    EXPECT_EQ(parse("7ksyyizzkutudzbv8aqztecjj", 36), expected);
}

TEST(FromChars, EveryBase_0x1p127) {
    const big_int expected = 1_n << 127;
    EXPECT_EQ(parse("10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 2), expected);
    EXPECT_EQ(parse("101100201022001010121000102002120122110122221010202000122201220121120010200022002", 3), expected);
    EXPECT_EQ(parse("2000000000000000000000000000000000000000000000000000000000000000", 4), expected);
    EXPECT_EQ(parse("3013030220323124042102424341431241221233040112312340403", 5), expected);
    EXPECT_EQ(parse("11324454543055553250455021551551121442554522203132", 6), expected);
    EXPECT_EQ(parse("1406241064412313155000336513424310163013142502", 7), expected);
    EXPECT_EQ(parse("2000000000000000000000000000000000000000000", 8), expected);
    EXPECT_EQ(parse("11321261117012076573587122018656546120262", 9), expected);
    EXPECT_EQ(parse("170141183460469231731687303715884105728", 10), expected);
    EXPECT_EQ(parse("555a8020989a11327710815513a946a188727", 11), expected);
    EXPECT_EQ(parse("2a695925806818735399a37a20a31b3534a8", 12), expected);
    EXPECT_EQ(parse("2373464c8a3cb25ba2b7c6382b2963bb71b", 13), expected);
    EXPECT_EQ(parse("27c22d5b9734a1517bb1dc612904a79d72", 14), expected);
    EXPECT_EQ(parse("3e2480b3404d8bb9bca3084369ba3e188", 15), expected);
    EXPECT_EQ(parse("80000000000000000000000000000000", 16), expected);
    EXPECT_EQ(parse("13d03cge4242f3e39f9dga60476a8099", 17), expected);
    EXPECT_EQ(parse("3d51ddf66g5befc8e19d2607hc26e32", 18), expected);
    EXPECT_EQ(parse("e09c09h6a4eihac8fchc875gf4di42", 19), expected);
    EXPECT_EQ(parse("337d04g0ec2d918ac3j85180dfd468", 20), expected);
    EXPECT_EQ(parse("g3b663ge01jk6cica417i3a75c602", 21), expected);
    EXPECT_EQ(parse("48f5dj8i8eli87ecigb8g6egjhchi", 22), expected);
    EXPECT_EQ(parse("162g6gam6d49ik37jk2mdcl41aj6i", 23), expected);
    EXPECT_EQ(parse("95b794mjicl2m0cbfjnjnd2hdm58", 24), expected);
    EXPECT_EQ(parse("31ffc3d7km5eej9ge7bdfk6d7j43", 25), expected);
    EXPECT_EQ(parse("11gf2k68m8of9j9agmk61a6g4i0o", 26), expected);
    EXPECT_EQ(parse("a9j813g0b2fhchp3k0hjogf3i82", 27), expected);
    EXPECT_EQ(parse("40j3ek89l5h69q3q6jh2khn6dog", 28), expected);
    EXPECT_EQ(parse("1hp3d3elmjg86isj584dklf2djr", 29), expected);
    EXPECT_EQ(parse("k2chs8jacc5g718abkqgokq448", 30), expected);
    EXPECT_EQ(parse("8q7cd4uoh31nng3aqs4edr0p74", 31), expected);
    EXPECT_EQ(parse("40000000000000000000000000", 32), expected);
    EXPECT_EQ(parse("1s5acer890k5h07uh412q2mo0t", 33), expected);
    EXPECT_EQ(parse("ttq3btpo06a5neskugckddplq", 34), expected);
    EXPECT_EQ(parse("evh2xn5qudpcldl13shtyuixn", 35), expected);
    EXPECT_EQ(parse("7ksyyizzkutudzbv8aqztecjk", 36), expected);
}

TEST(FromChars, EveryBase_0x1p200) {
    const big_int expected = 1_n << 200;
    EXPECT_EQ(parse("100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 2), expected);
    EXPECT_EQ(parse("1020010020011122222010012002211022002220100012201102121001102021011020010001110010111202101022110112000001121101020022221002011", 3), expected);
    EXPECT_EQ(parse("10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 4), expected);
    EXPECT_EQ(parse("111020132102420112021343001342032333120043341314112104422342034202402044234211314121001", 5), expected);
    EXPECT_EQ(parse("153532332401011220403425141024425043540104001011245043050435125240351024502304", 6), expected);
    EXPECT_EQ(parse("141246066533632643213232344050606053061443446006544361632102630555343054", 7), expected);
    EXPECT_EQ(parse("4000000000000000000000000000000000000000000000000000000000000000000", 8), expected);
    EXPECT_EQ(parse("1203204588105084262810181377042234203043114671273460047336287064", 9), expected);
    EXPECT_EQ(parse("1606938044258990275541962092341162602522202993782792835301376", 10), expected);
    EXPECT_EQ(parse("702a695236a26a175662a9a2048793aa12225aa884798921aa43640211", 11), expected);
    EXPECT_EQ(parse("711a44b68618019a2aa233ab1b3a354234329741725b37749147a994", 12), expected);
    EXPECT_EQ(parse("118c655a2aa24b3c3c25c8249269882811a1367a2b7c5b60b930499", 13), expected);
    EXPECT_EQ(parse("409840b05276d868d7a284750dd66851c40284971c8d464973c64", 14), expected);
    EXPECT_EQ(parse("1a306ea66c3a28ae9e2acb154bda45d3d33bcc419cb2a50c1d51", 15), expected);
    EXPECT_EQ(parse("100000000000000000000000000000000000000000000000000", 16), expected);
    EXPECT_EQ(parse("dg16f38eebf846a55ce10e44gf26g5fb8eg148ad8b5g84f11", 17), expected);
    EXPECT_EQ(parse("g2ceg0g4b8aa15g1hh662aacbhbd5hab7287a54c7b3d8dc4", 18), expected);
    EXPECT_EQ(parse("15359dfh0ccef9hgch451hg8fdb3c1e569cce46a98c2efi4", 19), expected);
    EXPECT_EQ(parse("25d8f83ed4e8idac0b962jghhebge99jd0f5bc06i10cd8g", 20), expected);
    EXPECT_EQ(parse("51fdh8f51670kddg476b2ih0ai1j9a55j48dk27be7c0i4", 21), expected);
    EXPECT_EQ(parse("dh5ih81f0403j656bd7i9ch363icjeb78gi7f4cki2h0c", 22), expected);
    EXPECT_EQ(parse("1lj985917fa09i739cl96kl9gc1978d7kbelg1dkcfde4", 23), expected);
    EXPECT_EQ(parse("74f57n8nnacman4gga8nb9dbemn4ifbe2bcfcjh0bkag", 24), expected);
    EXPECT_EQ(parse("1621h5ea6abjf1jahiga4ilgl75ocjajae24mjb89751", 25), expected);
    EXPECT_EQ(parse("5pg9dj501bcj73ompgoo20j8215ldjpmf56eo95mkhm", 26), expected);
    EXPECT_EQ(parse("16364hq352m82o95jbg1b74631c3dka8ce01ga68p24", 27), expected);
    EXPECT_EQ(parse("7clja95f6j5hiaikcg50ib2433rrikejonpi0896h4", 28), expected);
    EXPECT_EQ(parse("1m8gen8rkhnnhs0a3qf5fajn361kf4ae06pmrf91pg", 29), expected);
    EXPECT_EQ(parse("d6fm5miet05hlefp0n0d9chmptojk99cf225jf72g", 30), expected);
    EXPECT_EQ(parse("3hboprqof3jja07q3c5qmnja0tuc0chm0fffbg691", 31), expected);
    EXPECT_EQ(parse("10000000000000000000000000000000000000000", 32), expected);
    EXPECT_EQ(parse("9l10o238v9in0ega68m2g7rv989opaac2s0bajq1", 33), expected);
    EXPECT_EQ(parse("309k9mmbxji2j3ufnp7tpu8gi73a8tj2u823nxhi", 34), expected);
    EXPECT_EQ(parse("xysp95s4kpn5qxodmbh36dg7i7pnpt8lg7ur2jb", 35), expected);
    EXPECT_EQ(parse("bnklg118comha6gqury14067gur54n8won6guf4", 36), expected);
}

TEST(FromChars, EveryBaseLeadingZeros_0x1p200) {
    const big_int expected = 1_n << 200;
    // While the amount of leading zeroes here may seem excessive,
    // the point is to have one or several whole limbs worth of zeros.
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 2), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001020010020011122222010012002211022002220100012201102121001102021011020010001110010111202101022110112000001121101020022221002011", 3), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", 4), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000111020132102420112021343001342032333120043341314112104422342034202402044234211314121001", 5), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000153532332401011220403425141024425043540104001011245043050435125240351024502304", 6), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000141246066533632643213232344050606053061443446006544361632102630555343054", 7), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004000000000000000000000000000000000000000000000000000000000000000000", 8), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001203204588105084262810181377042234203043114671273460047336287064", 9), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001606938044258990275541962092341162602522202993782792835301376", 10), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000702a695236a26a175662a9a2048793aa12225aa884798921aa43640211", 11), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000711a44b68618019a2aa233ab1b3a354234329741725b37749147a994", 12), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000118c655a2aa24b3c3c25c8249269882811a1367a2b7c5b60b930499", 13), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000409840b05276d868d7a284750dd66851c40284971c8d464973c64", 14), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001a306ea66c3a28ae9e2acb154bda45d3d33bcc419cb2a50c1d51", 15), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000", 16), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000dg16f38eebf846a55ce10e44gf26g5fb8eg148ad8b5g84f11", 17), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000g2ceg0g4b8aa15g1hh662aacbhbd5hab7287a54c7b3d8dc4", 18), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000015359dfh0ccef9hgch451hg8fdb3c1e569cce46a98c2efi4", 19), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000025d8f83ed4e8idac0b962jghhebge99jd0f5bc06i10cd8g", 20), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000051fdh8f51670kddg476b2ih0ai1j9a55j48dk27be7c0i4", 21), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000dh5ih81f0403j656bd7i9ch363icjeb78gi7f4cki2h0c", 22), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001lj985917fa09i739cl96kl9gc1978d7kbelg1dkcfde4", 23), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000074f57n8nnacman4gga8nb9dbemn4ifbe2bcfcjh0bkag", 24), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001621h5ea6abjf1jahiga4ilgl75ocjajae24mjb89751", 25), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005pg9dj501bcj73ompgoo20j8215ldjpmf56eo95mkhm", 26), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000016364hq352m82o95jbg1b74631c3dka8ce01ga68p24", 27), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007clja95f6j5hiaikcg50ib2433rrikejonpi0896h4", 28), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001m8gen8rkhnnhs0a3qf5fajn361kf4ae06pmrf91pg", 29), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000d6fm5miet05hlefp0n0d9chmptojk99cf225jf72g", 30), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003hboprqof3jja07q3c5qmnja0tuc0chm0fffbg691", 31), expected);
    EXPECT_EQ(parse("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000", 32), expected);
    EXPECT_EQ(parse("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000009l10o238v9in0ega68m2g7rv989opaac2s0bajq1", 33), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000309k9mmbxji2j3ufnp7tpu8gi73a8tj2u823nxhi", 34), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000xysp95s4kpn5qxodmbh36dg7i7pnpt8lg7ur2jb", 35), expected);
    EXPECT_EQ(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000bnklg118comha6gqury14067gur54n8won6guf4", 36), expected);
}

TEST(FromChars, EveryBase_1234567890123456789012345678901234567890112233445566778899) {
    const big_int expected = 1234567890123456789012345678901234567890112233445566778899_n;
    EXPECT_EQ(parse("1100100101100101111101011111010010101011010111010110000000010101011010001111000011110010000101101011001110110000010011101110011110001010001000010010010011111010010110111101010000111000010011", 2), expected);
    EXPECT_EQ(parse("200112211110102021212200222012120002011210101222201200002000101122212221111212022122211110201211211110122020212122000000", 3), expected);
    EXPECT_EQ(parse("30211211331133102223113112000111122033003302011223032300103232132022020102103322112331100320103", 4), expected);
    EXPECT_EQ(parse("2443030312003032423221132100412422141210014133224140342114014311130233010043411044", 5), expected);
    EXPECT_EQ(parse("15334321411452312252221453151303140525001150011340242051412445054523101043", 6), expected);
    EXPECT_EQ(parse("26440656114205151522424022616202405154534130314565642632315310540442", 7), expected);
    EXPECT_EQ(parse("1445457537225327260025321703620553166023563612102223722675207023", 8), expected);
    EXPECT_EQ(parse("615743367780865502153358650060348787455278743654743566778000", 9), expected);
    EXPECT_EQ(parse("1234567890123456789012345678901234567890112233445566778899", 10), expected);
    EXPECT_EQ(parse("72017331781188537226214691a6861169a4a5a34a5556533343982", 11), expected);
    EXPECT_EQ(parse("95072955358a5a5958917776724801051a7700981b01076971783", 12), expected);
    EXPECT_EQ(parse("1ba4268b1cba8618c75baba39769b2424353a8783013593510b9", 13), expected);
    EXPECT_EQ(parse("877010525452793a66c39bb601492084683853c6222b250d59", 14), expected);
    EXPECT_EQ(parse("455312ee1037c1d82b7821ba032748dadb6411716c18b1d69", 15), expected);
    EXPECT_EQ(parse("32597d7d2ad758055a3c3c85acec13b9e288493e96f50e13", 16), expected);
    EXPECT_EQ(parse("31aeef86cef56442g4220366g988a26bb8f760e3078b9d1", 17), expected);
    EXPECT_EQ(parse("406a0h99fh5dfa2d0ahd0036d8g8bf8h2d2dg39c62d6d9", 18), expected);
    EXPECT_EQ(parse("6d7525g4h9ea7g0a98g9afa5g3i40d07f41507ac247h1", 19), expected);
    EXPECT_EQ(parse("e0e35i40eg2286j8hg32bf8jb3f44aegicfij6jc774j", 20), expected);
    EXPECT_EQ(parse("1f3a7ac32bec3fh1dei96hkfbhj2i4ck71f454g516f9", 21), expected);
    EXPECT_EQ(parse("52gjd917fkl2la9e68ac141jc2fb0330h99h0k0l29d", 22), expected);
    EXPECT_EQ(parse("i543gdle859f40efkheafjm1870d97bl1id8dghdmi", 23), expected);
    EXPECT_EQ(parse("349bd97fh5ig7dc36d7817dhbe7mh8e40b74a14gm3", 24), expected);
    EXPECT_EQ(parse("en3370fhmhb8b0lec9751liclkjb91n682i10nl5o", 25), expected);
    EXPECT_EQ(parse("32lckgdd55kpdmhii2nhjebd41426i262d1449mp9", 26), expected);
    EXPECT_EQ(parse("iemcb7niq5f24laqji20ahnpdn8hmcjmmch6nh00", 27), expected);
    EXPECT_EQ(parse("4dl0bb0blfaoipc8f7elhr5d7p0n28iob4rhmo9n", 28), expected);
    EXPECT_EQ(parse("14446650lm8disc37ohldq5f36dn51dbqrjmclll", 29), expected);
    EXPECT_EQ(parse("9457qs5cc4cjgsslml19i074rnl82ohein77m39", 30), expected);
    EXPECT_EQ(parse("2jfat1a95mrgd7ej9ducbir25ouseir6e53el5p", 31), expected);
    EXPECT_EQ(parse("p5ivbt5bblg1aq7gu8bb7c2esu52297qbfa3gj", 32), expected);
    EXPECT_EQ(parse("822lnwm95hb4bj95ulwgpdln3q8s81okhbjn2o", 33), expected);
    EXPECT_EQ(parse("2msi9cfaccqunvoom5m362coqrxcmttlqr3jf1", 34), expected);
    EXPECT_EQ(parse("vyrcqwj3tp1erk7nkx7xw4xme63wchx6ojh99", 35), expected);
    EXPECT_EQ(parse("blrdpawjeweaxb93a5h07u19ogcvpgt5tf66r", 36), expected);
}

TEST(FromChars, EveryBaseEmptyString) {
    static constexpr std::string_view empty = "";
    static constexpr std::from_chars_result expected{empty.data(), std::errc::invalid_argument};

    big_int result;
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 2), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 3), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 4), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 5), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 6), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 7), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 8), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 9), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 10), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 11), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 12), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 13), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 14), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 15), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 16), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 17), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 18), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 19), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 20), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 21), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 22), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 23), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 24), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 25), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 26), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 27), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 28), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 29), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 30), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 31), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 32), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 33), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 34), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 35), expected);
    EXPECT_EQ(from_chars(empty.data(), empty.data(), result, 36), expected);
}

TEST(FromChars, EveryBaseInvalidCharacter) {
    static constexpr std::string_view invalid = "@";
    static constexpr std::from_chars_result expected{invalid.data() + invalid.size(), std::errc::invalid_argument};

    big_int result;
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 2), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 3), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 4), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 5), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 6), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 7), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 8), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 9), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 10), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 11), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 12), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 13), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 14), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 15), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 16), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 17), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 18), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 19), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 20), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 21), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 22), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 23), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 24), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 25), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 26), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 27), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 28), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 29), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 30), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 31), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 32), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 33), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 34), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 35), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 36), expected);
}

TEST(FromChars, EveryBaseInvalidCharacterAfterNegation) {
    static constexpr std::string_view invalid = "-@";
    static constexpr std::from_chars_result expected{invalid.data() + invalid.size(), std::errc::invalid_argument};

    big_int result;
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 2), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 3), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 4), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 5), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 6), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 7), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 8), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 9), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 10), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 11), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 12), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 13), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 14), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 15), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 16), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 17), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 18), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 19), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 20), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 21), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 22), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 23), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 24), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 25), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 26), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 27), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 28), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 29), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 30), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 31), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 32), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 33), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 34), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 35), expected);
    EXPECT_EQ(from_chars(invalid.data(), invalid.data() + invalid.size(), result, 36), expected);
}

TEST(FromChars, EveryBaseDigitTooLargeForBase) {
    static constexpr auto from_string = [](const std::string_view digit, int base) {
        big_int result;
        return from_chars(digit.data(), digit.data() + digit.size(), result, base);
    };
    EXPECT_EQ(from_string("2", 2).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("3", 3).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("4", 4).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("5", 5).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("6", 6).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("7", 7).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("8", 8).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("9", 9).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("a", 10).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("b", 11).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("c", 12).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("d", 13).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("e", 14).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("f", 15).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("g", 16).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("h", 17).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("i", 18).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("j", 19).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("k", 20).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("l", 21).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("m", 22).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("n", 23).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("o", 24).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("p", 25).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("q", 26).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("r", 27).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("s", 28).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("t", 29).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("u", 30).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("v", 31).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("w", 32).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("x", 33).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("y", 34).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("z", 35).ec, std::errc::invalid_argument);
}

TEST(FromChars, EveryBaseDigitTooLargeAfterNegation) {
    static constexpr auto from_string = [](const std::string_view digit, int base) {
        big_int result;
        return from_chars(digit.data(), digit.data() + digit.size(), result, base);
    };
    EXPECT_EQ(from_string("-2", 2).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-3", 3).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-4", 4).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-5", 5).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-6", 6).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-7", 7).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-8", 8).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-9", 9).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-a", 10).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-b", 11).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-c", 12).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-d", 13).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-e", 14).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-f", 15).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-g", 16).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-h", 17).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-i", 18).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-j", 19).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-k", 20).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-l", 21).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-m", 22).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-n", 23).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-o", 24).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-p", 25).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-q", 26).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-r", 27).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-s", 28).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-t", 29).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-u", 30).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-v", 31).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-w", 32).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-x", 33).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-y", 34).ec, std::errc::invalid_argument);
    EXPECT_EQ(from_string("-z", 35).ec, std::errc::invalid_argument);
}

TEST(ToChars, WidthMag) {
    EXPECT_EQ(big_int{-0x1p1000}.width_mag(), 1001);
    EXPECT_EQ(big_int{-0x1p100}.width_mag(), 101);
    EXPECT_EQ(big_int{-8}.width_mag(), 4);
    EXPECT_EQ(big_int{-7}.width_mag(), 3);
    EXPECT_EQ(big_int{-6}.width_mag(), 3);
    EXPECT_EQ(big_int{-5}.width_mag(), 3);
    EXPECT_EQ(big_int{-4}.width_mag(), 3);
    EXPECT_EQ(big_int{-3}.width_mag(), 2);
    EXPECT_EQ(big_int{-2}.width_mag(), 2);
    EXPECT_EQ(big_int{-1}.width_mag(), 1);
    EXPECT_EQ(big_int{0}.width_mag(), 0);
    EXPECT_EQ(big_int{1}.width_mag(), 1);
    EXPECT_EQ(big_int{2}.width_mag(), 2);
    EXPECT_EQ(big_int{3}.width_mag(), 2);
    EXPECT_EQ(big_int{4}.width_mag(), 3);
    EXPECT_EQ(big_int{5}.width_mag(), 3);
    EXPECT_EQ(big_int{6}.width_mag(), 3);
    EXPECT_EQ(big_int{7}.width_mag(), 3);
    EXPECT_EQ(big_int{8}.width_mag(), 4);
    EXPECT_EQ(big_int{0x1p100}.width_mag(), 101);
    EXPECT_EQ(big_int{0x1p1000}.width_mag(), 1001);
}

TEST(ToChars, EveryBase_255) {
    constexpr big_int value = 255;
    EXPECT_EQ(to_string(value, 2), "11111111");
    EXPECT_EQ(to_string(value, 3), "100110");
    EXPECT_EQ(to_string(value, 4), "3333");
    EXPECT_EQ(to_string(value, 5), "2010");
    EXPECT_EQ(to_string(value, 6), "1103");
    EXPECT_EQ(to_string(value, 7), "513");
    EXPECT_EQ(to_string(value, 8), "377");
    EXPECT_EQ(to_string(value, 9), "313");
    EXPECT_EQ(to_string(value, 10), "255");
    EXPECT_EQ(to_string(value, 11), "212");
    EXPECT_EQ(to_string(value, 12), "193");
    EXPECT_EQ(to_string(value, 13), "168");
    EXPECT_EQ(to_string(value, 14), "143");
    EXPECT_EQ(to_string(value, 15), "120");
    EXPECT_EQ(to_string(value, 16), "ff");
    EXPECT_EQ(to_string(value, 17), "f0");
    EXPECT_EQ(to_string(value, 18), "e3");
    EXPECT_EQ(to_string(value, 19), "d8");
    EXPECT_EQ(to_string(value, 20), "cf");
    EXPECT_EQ(to_string(value, 21), "c3");
    EXPECT_EQ(to_string(value, 22), "bd");
    EXPECT_EQ(to_string(value, 23), "b2");
    EXPECT_EQ(to_string(value, 24), "af");
    EXPECT_EQ(to_string(value, 25), "a5");
    EXPECT_EQ(to_string(value, 26), "9l");
    EXPECT_EQ(to_string(value, 27), "9c");
    EXPECT_EQ(to_string(value, 28), "93");
    EXPECT_EQ(to_string(value, 29), "8n");
    EXPECT_EQ(to_string(value, 30), "8f");
    EXPECT_EQ(to_string(value, 31), "87");
    EXPECT_EQ(to_string(value, 32), "7v");
    EXPECT_EQ(to_string(value, 33), "7o");
    EXPECT_EQ(to_string(value, 34), "7h");
    EXPECT_EQ(to_string(value, 35), "7a");
    EXPECT_EQ(to_string(value, 36), "73");
}

TEST(ToChars, EveryBase_m255) {
    constexpr big_int value = -255;
    EXPECT_EQ(to_string(value, 2), "-11111111");
    EXPECT_EQ(to_string(value, 3), "-100110");
    EXPECT_EQ(to_string(value, 4), "-3333");
    EXPECT_EQ(to_string(value, 5), "-2010");
    EXPECT_EQ(to_string(value, 6), "-1103");
    EXPECT_EQ(to_string(value, 7), "-513");
    EXPECT_EQ(to_string(value, 8), "-377");
    EXPECT_EQ(to_string(value, 9), "-313");
    EXPECT_EQ(to_string(value, 10), "-255");
    EXPECT_EQ(to_string(value, 11), "-212");
    EXPECT_EQ(to_string(value, 12), "-193");
    EXPECT_EQ(to_string(value, 13), "-168");
    EXPECT_EQ(to_string(value, 14), "-143");
    EXPECT_EQ(to_string(value, 15), "-120");
    EXPECT_EQ(to_string(value, 16), "-ff");
    EXPECT_EQ(to_string(value, 17), "-f0");
    EXPECT_EQ(to_string(value, 18), "-e3");
    EXPECT_EQ(to_string(value, 19), "-d8");
    EXPECT_EQ(to_string(value, 20), "-cf");
    EXPECT_EQ(to_string(value, 21), "-c3");
    EXPECT_EQ(to_string(value, 22), "-bd");
    EXPECT_EQ(to_string(value, 23), "-b2");
    EXPECT_EQ(to_string(value, 24), "-af");
    EXPECT_EQ(to_string(value, 25), "-a5");
    EXPECT_EQ(to_string(value, 26), "-9l");
    EXPECT_EQ(to_string(value, 27), "-9c");
    EXPECT_EQ(to_string(value, 28), "-93");
    EXPECT_EQ(to_string(value, 29), "-8n");
    EXPECT_EQ(to_string(value, 30), "-8f");
    EXPECT_EQ(to_string(value, 31), "-87");
    EXPECT_EQ(to_string(value, 32), "-7v");
    EXPECT_EQ(to_string(value, 33), "-7o");
    EXPECT_EQ(to_string(value, 34), "-7h");
    EXPECT_EQ(to_string(value, 35), "-7a");
    EXPECT_EQ(to_string(value, 36), "-73");
}

TEST(ToChars, EveryBase_0x1p64) {
    const big_int value = 1_n << 64;
    EXPECT_EQ(to_string(value, 2), "10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "11112220022122120101211020120210210211221");
    EXPECT_EQ(to_string(value, 4), "100000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "2214220303114400424121122431");
    EXPECT_EQ(to_string(value, 6), "3520522010102100444244424");
    EXPECT_EQ(to_string(value, 7), "45012021522523134134602");
    EXPECT_EQ(to_string(value, 8), "2000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "145808576354216723757");
    EXPECT_EQ(to_string(value, 10), "18446744073709551616");
    EXPECT_EQ(to_string(value, 11), "335500516a429071285");
    EXPECT_EQ(to_string(value, 12), "839365134a2a240714");
    EXPECT_EQ(to_string(value, 13), "219505a9511a867b73");
    EXPECT_EQ(to_string(value, 14), "8681049adb03db172");
    EXPECT_EQ(to_string(value, 15), "2c1d56b648c6cd111");
    EXPECT_EQ(to_string(value, 16), "10000000000000000");
    EXPECT_EQ(to_string(value, 17), "67979g60f5428011");
    EXPECT_EQ(to_string(value, 18), "2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_string(value, 19), "141c8786h1ccaagh");
    EXPECT_EQ(to_string(value, 20), "b53bjh07be4dj0g");
    EXPECT_EQ(to_string(value, 21), "5e8g4ggg7g56dig");
    EXPECT_EQ(to_string(value, 22), "2l4lf104353j8kg");
    EXPECT_EQ(to_string(value, 23), "1ddh88h2782i516");
    EXPECT_EQ(to_string(value, 24), "l12ee5fn0ji1ig");
    EXPECT_EQ(to_string(value, 25), "c9c336o0mlb7eg");
    EXPECT_EQ(to_string(value, 26), "7b7n2pcniokcgg");
    EXPECT_EQ(to_string(value, 27), "4eo8hfam6fllmp");
    EXPECT_EQ(to_string(value, 28), "2nc6j26l66rhog");
    EXPECT_EQ(to_string(value, 29), "1n3rsh11f098ro");
    EXPECT_EQ(to_string(value, 30), "14l9lkmo30o40g");
    EXPECT_EQ(to_string(value, 31), "nd075ib45k86g");
    EXPECT_EQ(to_string(value, 32), "g000000000000");
    EXPECT_EQ(to_string(value, 33), "b1w8p7j5q9r6g");
    EXPECT_EQ(to_string(value, 34), "7orp63sh4dphi");
    EXPECT_EQ(to_string(value, 35), "5g24a25twkwfg");
    EXPECT_EQ(to_string(value, 36), "3w5e11264sgsg");
}

TEST(ToChars, EveryBase_m0x1p64) {
    const big_int value = -(1_n << 64);
    EXPECT_EQ(to_string(value, 2), "-10000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "-11112220022122120101211020120210210211221");
    EXPECT_EQ(to_string(value, 4), "-100000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "-2214220303114400424121122431");
    EXPECT_EQ(to_string(value, 6), "-3520522010102100444244424");
    EXPECT_EQ(to_string(value, 7), "-45012021522523134134602");
    EXPECT_EQ(to_string(value, 8), "-2000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "-145808576354216723757");
    EXPECT_EQ(to_string(value, 10), "-18446744073709551616");
    EXPECT_EQ(to_string(value, 11), "-335500516a429071285");
    EXPECT_EQ(to_string(value, 12), "-839365134a2a240714");
    EXPECT_EQ(to_string(value, 13), "-219505a9511a867b73");
    EXPECT_EQ(to_string(value, 14), "-8681049adb03db172");
    EXPECT_EQ(to_string(value, 15), "-2c1d56b648c6cd111");
    EXPECT_EQ(to_string(value, 16), "-10000000000000000");
    EXPECT_EQ(to_string(value, 17), "-67979g60f5428011");
    EXPECT_EQ(to_string(value, 18), "-2d3fgb0b9cg4bd2g");
    EXPECT_EQ(to_string(value, 19), "-141c8786h1ccaagh");
    EXPECT_EQ(to_string(value, 20), "-b53bjh07be4dj0g");
    EXPECT_EQ(to_string(value, 21), "-5e8g4ggg7g56dig");
    EXPECT_EQ(to_string(value, 22), "-2l4lf104353j8kg");
    EXPECT_EQ(to_string(value, 23), "-1ddh88h2782i516");
    EXPECT_EQ(to_string(value, 24), "-l12ee5fn0ji1ig");
    EXPECT_EQ(to_string(value, 25), "-c9c336o0mlb7eg");
    EXPECT_EQ(to_string(value, 26), "-7b7n2pcniokcgg");
    EXPECT_EQ(to_string(value, 27), "-4eo8hfam6fllmp");
    EXPECT_EQ(to_string(value, 28), "-2nc6j26l66rhog");
    EXPECT_EQ(to_string(value, 29), "-1n3rsh11f098ro");
    EXPECT_EQ(to_string(value, 30), "-14l9lkmo30o40g");
    EXPECT_EQ(to_string(value, 31), "-nd075ib45k86g");
    EXPECT_EQ(to_string(value, 32), "-g000000000000");
    EXPECT_EQ(to_string(value, 33), "-b1w8p7j5q9r6g");
    EXPECT_EQ(to_string(value, 34), "-7orp63sh4dphi");
    EXPECT_EQ(to_string(value, 35), "-5g24a25twkwfg");
    EXPECT_EQ(to_string(value, 36), "-3w5e11264sgsg");
}

TEST(ToChars, EveryBase_0x1p127m1) {
    const big_int value = (1_n << 127) - 1;
    EXPECT_EQ(to_string(value, 2), "1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
    EXPECT_EQ(to_string(value, 3), "101100201022001010121000102002120122110122221010202000122201220121120010200022001");
    EXPECT_EQ(to_string(value, 4), "1333333333333333333333333333333333333333333333333333333333333333");
    EXPECT_EQ(to_string(value, 5), "3013030220323124042102424341431241221233040112312340402");
    EXPECT_EQ(to_string(value, 6), "11324454543055553250455021551551121442554522203131");
    EXPECT_EQ(to_string(value, 7), "1406241064412313155000336513424310163013142501");
    EXPECT_EQ(to_string(value, 8), "1777777777777777777777777777777777777777777");
    EXPECT_EQ(to_string(value, 9), "11321261117012076573587122018656546120261");
    EXPECT_EQ(to_string(value, 10), "170141183460469231731687303715884105727");
    EXPECT_EQ(to_string(value, 11), "555a8020989a11327710815513a946a188726");
    EXPECT_EQ(to_string(value, 12), "2a695925806818735399a37a20a31b3534a7");
    EXPECT_EQ(to_string(value, 13), "2373464c8a3cb25ba2b7c6382b2963bb71a");
    EXPECT_EQ(to_string(value, 14), "27c22d5b9734a1517bb1dc612904a79d71");
    EXPECT_EQ(to_string(value, 15), "3e2480b3404d8bb9bca3084369ba3e187");
    EXPECT_EQ(to_string(value, 16), "7fffffffffffffffffffffffffffffff");
    EXPECT_EQ(to_string(value, 17), "13d03cge4242f3e39f9dga60476a8098");
    EXPECT_EQ(to_string(value, 18), "3d51ddf66g5befc8e19d2607hc26e31");
    EXPECT_EQ(to_string(value, 19), "e09c09h6a4eihac8fchc875gf4di41");
    EXPECT_EQ(to_string(value, 20), "337d04g0ec2d918ac3j85180dfd467");
    EXPECT_EQ(to_string(value, 21), "g3b663ge01jk6cica417i3a75c601");
    EXPECT_EQ(to_string(value, 22), "48f5dj8i8eli87ecigb8g6egjhchh");
    EXPECT_EQ(to_string(value, 23), "162g6gam6d49ik37jk2mdcl41aj6h");
    EXPECT_EQ(to_string(value, 24), "95b794mjicl2m0cbfjnjnd2hdm57");
    EXPECT_EQ(to_string(value, 25), "31ffc3d7km5eej9ge7bdfk6d7j42");
    EXPECT_EQ(to_string(value, 26), "11gf2k68m8of9j9agmk61a6g4i0n");
    EXPECT_EQ(to_string(value, 27), "a9j813g0b2fhchp3k0hjogf3i81");
    EXPECT_EQ(to_string(value, 28), "40j3ek89l5h69q3q6jh2khn6dof");
    EXPECT_EQ(to_string(value, 29), "1hp3d3elmjg86isj584dklf2djq");
    EXPECT_EQ(to_string(value, 30), "k2chs8jacc5g718abkqgokq447");
    EXPECT_EQ(to_string(value, 31), "8q7cd4uoh31nng3aqs4edr0p73");
    EXPECT_EQ(to_string(value, 32), "3vvvvvvvvvvvvvvvvvvvvvvvvv");
    EXPECT_EQ(to_string(value, 33), "1s5acer890k5h07uh412q2mo0s");
    EXPECT_EQ(to_string(value, 34), "ttq3btpo06a5neskugckddplp");
    EXPECT_EQ(to_string(value, 35), "evh2xn5qudpcldl13shtyuixm");
    EXPECT_EQ(to_string(value, 36), "7ksyyizzkutudzbv8aqztecjj");
}

TEST(ToChars, EveryBase_0x1p127) {
    const big_int value = 1_n << 127;
    EXPECT_EQ(to_string(value, 2), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "101100201022001010121000102002120122110122221010202000122201220121120010200022002");
    EXPECT_EQ(to_string(value, 4), "2000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "3013030220323124042102424341431241221233040112312340403");
    EXPECT_EQ(to_string(value, 6), "11324454543055553250455021551551121442554522203132");
    EXPECT_EQ(to_string(value, 7), "1406241064412313155000336513424310163013142502");
    EXPECT_EQ(to_string(value, 8), "2000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "11321261117012076573587122018656546120262");
    EXPECT_EQ(to_string(value, 10), "170141183460469231731687303715884105728");
    EXPECT_EQ(to_string(value, 11), "555a8020989a11327710815513a946a188727");
    EXPECT_EQ(to_string(value, 12), "2a695925806818735399a37a20a31b3534a8");
    EXPECT_EQ(to_string(value, 13), "2373464c8a3cb25ba2b7c6382b2963bb71b");
    EXPECT_EQ(to_string(value, 14), "27c22d5b9734a1517bb1dc612904a79d72");
    EXPECT_EQ(to_string(value, 15), "3e2480b3404d8bb9bca3084369ba3e188");
    EXPECT_EQ(to_string(value, 16), "80000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 17), "13d03cge4242f3e39f9dga60476a8099");
    EXPECT_EQ(to_string(value, 18), "3d51ddf66g5befc8e19d2607hc26e32");
    EXPECT_EQ(to_string(value, 19), "e09c09h6a4eihac8fchc875gf4di42");
    EXPECT_EQ(to_string(value, 20), "337d04g0ec2d918ac3j85180dfd468");
    EXPECT_EQ(to_string(value, 21), "g3b663ge01jk6cica417i3a75c602");
    EXPECT_EQ(to_string(value, 22), "48f5dj8i8eli87ecigb8g6egjhchi");
    EXPECT_EQ(to_string(value, 23), "162g6gam6d49ik37jk2mdcl41aj6i");
    EXPECT_EQ(to_string(value, 24), "95b794mjicl2m0cbfjnjnd2hdm58");
    EXPECT_EQ(to_string(value, 25), "31ffc3d7km5eej9ge7bdfk6d7j43");
    EXPECT_EQ(to_string(value, 26), "11gf2k68m8of9j9agmk61a6g4i0o");
    EXPECT_EQ(to_string(value, 27), "a9j813g0b2fhchp3k0hjogf3i82");
    EXPECT_EQ(to_string(value, 28), "40j3ek89l5h69q3q6jh2khn6dog");
    EXPECT_EQ(to_string(value, 29), "1hp3d3elmjg86isj584dklf2djr");
    EXPECT_EQ(to_string(value, 30), "k2chs8jacc5g718abkqgokq448");
    EXPECT_EQ(to_string(value, 31), "8q7cd4uoh31nng3aqs4edr0p74");
    EXPECT_EQ(to_string(value, 32), "40000000000000000000000000");
    EXPECT_EQ(to_string(value, 33), "1s5acer890k5h07uh412q2mo0t");
    EXPECT_EQ(to_string(value, 34), "ttq3btpo06a5neskugckddplq");
    EXPECT_EQ(to_string(value, 35), "evh2xn5qudpcldl13shtyuixn");
    EXPECT_EQ(to_string(value, 36), "7ksyyizzkutudzbv8aqztecjk");
}

TEST(ToChars, EveryBase_0x1p200) {
    const big_int value = 1_n << 200;
    EXPECT_EQ(to_string(value, 2), "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 3), "1020010020011122222010012002211022002220100012201102121001102021011020010001110010111202101022110112000001121101020022221002011");
    EXPECT_EQ(to_string(value, 4), "10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 5), "111020132102420112021343001342032333120043341314112104422342034202402044234211314121001");
    EXPECT_EQ(to_string(value, 6), "153532332401011220403425141024425043540104001011245043050435125240351024502304");
    EXPECT_EQ(to_string(value, 7), "141246066533632643213232344050606053061443446006544361632102630555343054");
    EXPECT_EQ(to_string(value, 8), "4000000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 9), "1203204588105084262810181377042234203043114671273460047336287064");
    EXPECT_EQ(to_string(value, 10), "1606938044258990275541962092341162602522202993782792835301376");
    EXPECT_EQ(to_string(value, 11), "702a695236a26a175662a9a2048793aa12225aa884798921aa43640211");
    EXPECT_EQ(to_string(value, 12), "711a44b68618019a2aa233ab1b3a354234329741725b37749147a994");
    EXPECT_EQ(to_string(value, 13), "118c655a2aa24b3c3c25c8249269882811a1367a2b7c5b60b930499");
    EXPECT_EQ(to_string(value, 14), "409840b05276d868d7a284750dd66851c40284971c8d464973c64");
    EXPECT_EQ(to_string(value, 15), "1a306ea66c3a28ae9e2acb154bda45d3d33bcc419cb2a50c1d51");
    EXPECT_EQ(to_string(value, 16), "100000000000000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 17), "dg16f38eebf846a55ce10e44gf26g5fb8eg148ad8b5g84f11");
    EXPECT_EQ(to_string(value, 18), "g2ceg0g4b8aa15g1hh662aacbhbd5hab7287a54c7b3d8dc4");
    EXPECT_EQ(to_string(value, 19), "15359dfh0ccef9hgch451hg8fdb3c1e569cce46a98c2efi4");
    EXPECT_EQ(to_string(value, 20), "25d8f83ed4e8idac0b962jghhebge99jd0f5bc06i10cd8g");
    EXPECT_EQ(to_string(value, 21), "51fdh8f51670kddg476b2ih0ai1j9a55j48dk27be7c0i4");
    EXPECT_EQ(to_string(value, 22), "dh5ih81f0403j656bd7i9ch363icjeb78gi7f4cki2h0c");
    EXPECT_EQ(to_string(value, 23), "1lj985917fa09i739cl96kl9gc1978d7kbelg1dkcfde4");
    EXPECT_EQ(to_string(value, 24), "74f57n8nnacman4gga8nb9dbemn4ifbe2bcfcjh0bkag");
    EXPECT_EQ(to_string(value, 25), "1621h5ea6abjf1jahiga4ilgl75ocjajae24mjb89751");
    EXPECT_EQ(to_string(value, 26), "5pg9dj501bcj73ompgoo20j8215ldjpmf56eo95mkhm");
    EXPECT_EQ(to_string(value, 27), "16364hq352m82o95jbg1b74631c3dka8ce01ga68p24");
    EXPECT_EQ(to_string(value, 28), "7clja95f6j5hiaikcg50ib2433rrikejonpi0896h4");
    EXPECT_EQ(to_string(value, 29), "1m8gen8rkhnnhs0a3qf5fajn361kf4ae06pmrf91pg");
    EXPECT_EQ(to_string(value, 30), "d6fm5miet05hlefp0n0d9chmptojk99cf225jf72g");
    EXPECT_EQ(to_string(value, 31), "3hboprqof3jja07q3c5qmnja0tuc0chm0fffbg691");
    EXPECT_EQ(to_string(value, 32), "10000000000000000000000000000000000000000");
    EXPECT_EQ(to_string(value, 33), "9l10o238v9in0ega68m2g7rv989opaac2s0bajq1");
    EXPECT_EQ(to_string(value, 34), "309k9mmbxji2j3ufnp7tpu8gi73a8tj2u823nxhi");
    EXPECT_EQ(to_string(value, 35), "xysp95s4kpn5qxodmbh36dg7i7pnpt8lg7ur2jb");
    EXPECT_EQ(to_string(value, 36), "bnklg118comha6gqury14067gur54n8won6guf4");
}

TEST(ToChars, EveryBase_1234567890123456789012345678901234567890112233445566778899) {
    const big_int value = 1234567890123456789012345678901234567890112233445566778899_n;
    EXPECT_EQ(to_string(value, 2), "1100100101100101111101011111010010101011010111010110000000010101011010001111000011110010000101101011001110110000010011101110011110001010001000010010010011111010010110111101010000111000010011");
    EXPECT_EQ(to_string(value, 3), "200112211110102021212200222012120002011210101222201200002000101122212221111212022122211110201211211110122020212122000000");
    EXPECT_EQ(to_string(value, 4), "30211211331133102223113112000111122033003302011223032300103232132022020102103322112331100320103");
    EXPECT_EQ(to_string(value, 5), "2443030312003032423221132100412422141210014133224140342114014311130233010043411044");
    EXPECT_EQ(to_string(value, 6), "15334321411452312252221453151303140525001150011340242051412445054523101043");
    EXPECT_EQ(to_string(value, 7), "26440656114205151522424022616202405154534130314565642632315310540442");
    EXPECT_EQ(to_string(value, 8), "1445457537225327260025321703620553166023563612102223722675207023");
    EXPECT_EQ(to_string(value, 9), "615743367780865502153358650060348787455278743654743566778000");
    EXPECT_EQ(to_string(value, 10), "1234567890123456789012345678901234567890112233445566778899");
    EXPECT_EQ(to_string(value, 11), "72017331781188537226214691a6861169a4a5a34a5556533343982");
    EXPECT_EQ(to_string(value, 12), "95072955358a5a5958917776724801051a7700981b01076971783");
    EXPECT_EQ(to_string(value, 13), "1ba4268b1cba8618c75baba39769b2424353a8783013593510b9");
    EXPECT_EQ(to_string(value, 14), "877010525452793a66c39bb601492084683853c6222b250d59");
    EXPECT_EQ(to_string(value, 15), "455312ee1037c1d82b7821ba032748dadb6411716c18b1d69");
    EXPECT_EQ(to_string(value, 16), "32597d7d2ad758055a3c3c85acec13b9e288493e96f50e13");
    EXPECT_EQ(to_string(value, 17), "31aeef86cef56442g4220366g988a26bb8f760e3078b9d1");
    EXPECT_EQ(to_string(value, 18), "406a0h99fh5dfa2d0ahd0036d8g8bf8h2d2dg39c62d6d9");
    EXPECT_EQ(to_string(value, 19), "6d7525g4h9ea7g0a98g9afa5g3i40d07f41507ac247h1");
    EXPECT_EQ(to_string(value, 20), "e0e35i40eg2286j8hg32bf8jb3f44aegicfij6jc774j");
    EXPECT_EQ(to_string(value, 21), "1f3a7ac32bec3fh1dei96hkfbhj2i4ck71f454g516f9");
    EXPECT_EQ(to_string(value, 22), "52gjd917fkl2la9e68ac141jc2fb0330h99h0k0l29d");
    EXPECT_EQ(to_string(value, 23), "i543gdle859f40efkheafjm1870d97bl1id8dghdmi");
    EXPECT_EQ(to_string(value, 24), "349bd97fh5ig7dc36d7817dhbe7mh8e40b74a14gm3");
    EXPECT_EQ(to_string(value, 25), "en3370fhmhb8b0lec9751liclkjb91n682i10nl5o");
    EXPECT_EQ(to_string(value, 26), "32lckgdd55kpdmhii2nhjebd41426i262d1449mp9");
    EXPECT_EQ(to_string(value, 27), "iemcb7niq5f24laqji20ahnpdn8hmcjmmch6nh00");
    EXPECT_EQ(to_string(value, 28), "4dl0bb0blfaoipc8f7elhr5d7p0n28iob4rhmo9n");
    EXPECT_EQ(to_string(value, 29), "14446650lm8disc37ohldq5f36dn51dbqrjmclll");
    EXPECT_EQ(to_string(value, 30), "9457qs5cc4cjgsslml19i074rnl82ohein77m39");
    EXPECT_EQ(to_string(value, 31), "2jfat1a95mrgd7ej9ducbir25ouseir6e53el5p");
    EXPECT_EQ(to_string(value, 32), "p5ivbt5bblg1aq7gu8bb7c2esu52297qbfa3gj");
    EXPECT_EQ(to_string(value, 33), "822lnwm95hb4bj95ulwgpdln3q8s81okhbjn2o");
    EXPECT_EQ(to_string(value, 34), "2msi9cfaccqunvoom5m362coqrxcmttlqr3jf1");
    EXPECT_EQ(to_string(value, 35), "vyrcqwj3tp1erk7nkx7xw4xme63wchx6ojh99");
    EXPECT_EQ(to_string(value, 36), "blrdpawjeweaxb93a5h07u19ogcvpgt5tf66r");
}
// clang-format on

TEST(ToChars, EmptyRangeValueTooLarge) {
    const big_int values[]{
        0,
        1,
        -1,
        255,
        -255,
        1_n << 128,
        -(1_n << 128),
    };
    char range;
    for (const auto& value : values) {
        for (int base = 2; base < 36; ++base) {
            const auto [p, ec] = to_chars(&range, &range, value, base);
            EXPECT_EQ(ec, std::errc::value_too_large);
        }
    }
}

TEST(ToChars, OnlyMinusValueTooLarge) {
    const big_int values[]{
        -1,
        -(1_n << 128),
    };
    char range;
    for (const auto& value : values) {
        for (int base = 2; base < 36; ++base) {
            const auto [p, ec] = to_chars(&range, &range + 1, value, base);
            EXPECT_EQ(ec, std::errc::value_too_large);
        }
    }
}

TEST(ToChars, HugeValueTooLarge) {
    const big_int value = 1_n << 128;
    char          range[8];
    for (int base = 2; base < 36; ++base) {
        const auto [p, ec] = to_chars(range, std::end(range), value, base);
        EXPECT_EQ(ec, std::errc::value_too_large);
    }
}

} // namespace
