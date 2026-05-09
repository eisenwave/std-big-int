// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <iostream>
#include <span>

using beman::big_int::big_int;

template <class LimbType>
static auto msb_limb(LimbType x) -> int {
    if (x == LimbType{0}) {
        return -1;
    }

    constexpr LimbType limb_mask{LimbType{1} << (std::numeric_limits<LimbType>::digits - 1)};

    int bpos{};

    while ((x & limb_mask) == LimbType{0}) {
        ++bpos;
        x <<= 1;
    }

    return (std::numeric_limits<LimbType>::digits - 1) - bpos;
}

static auto msb(const big_int& m) -> int {

    const auto hi_limb{m.representation().back()};

    int bpos{msb_limb(hi_limb)};

    bpos += static_cast<int>(m.representation().size() - 1U) *
            std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits;

    return bpos;
}

static auto sqrt(big_int m) -> big_int {
    // Calculate the square root.

    big_int s{};

    if (m <= 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const auto msb_pos = msb(m);

        const auto msb_pos_mod_2 = msb_pos % 2;

        // Obtain the initial value.
        const auto left_shift_amount = 1 + ((msb_pos_mod_2 == 0) ? msb_pos / 2 : (msb_pos + 1) / 2);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the square root.
        // See Algorithm 1.13 SqrtInt, Sect. 1.5.1
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            u = (s + (m / s)) / 2;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

static auto cbrt(big_int m) -> big_int {
    // Calculate the cube root.

    big_int s;

    if (m < 0) {
        s = -cbrt(-m);
    } else if (m == 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const auto msb_pos = msb(m);

        // Obtain the initial value.
        const auto msb_pos_mod_3 = msb_pos % 3;

        const auto left_shift_amount =
            1 + ((msb_pos_mod_3 == 0) ? 1 + msb_pos / 3 : (msb_pos + (3 - msb_pos_mod_3)) / 3);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the k'th root (applied for k = 3).
        // See Algorithm 1.14 RootInt, Sect. 1.5.2
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        constexpr auto three_minus_one = 3 - 1;

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            big_int m_over_s_pow_3_minus_one(m);

            // Use an unrolled loop to divide by s^2 here.
            m_over_s_pow_3_minus_one /= s;
            m_over_s_pow_3_minus_one /= s;

            u = ((s * three_minus_one) + m_over_s_pow_3_minus_one) / 3;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

inline static constexpr char arg_digits[] =
    "3"
    "1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679"
    "8214808651328230664709384460955058223172535940812848111745028410270193852110555964462294895493038196"
    "4428810975665933446128475648233786783165271201909145648566923460348610454326648213393607260249141273"
    "7245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094"
    "3305727036575959195309218611738193261179310511854807446237996274956735188575272489122793818301194912"
    "9833673362440656643086021394946395224737190702179860943702770539217176293176752384674818467669405132"
    "0005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235"
    "4201995611212902196086403441815981362977477130996051870721134999999837297804995105973173281609631859"
    "5024459455346908302642522308253344685035261931188171010003137838752886587533208381420617177669147303"
    "5982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989";

inline static constexpr char ctrl_sqrt[] =
    "1772453850905516027298167483341145182797549456122387128213807789852911284591032181374950656738544665"
    "4162268236242825706662361528657244226025250937096027870684620376986531051228499251730289508262289320"
    "9537926796280017463901535147972051670019018523401858544697449491264031392177552590621640541933250090"
    "6398407613733477475153433667989789365851836408795451165161738760059067393431791332809854846248184902"
    "0546548521956132515616474675150427387610561079961271072100603720444836723652966137080943234988316684"
    "2";

inline static constexpr char ctrl_cbrt[] =
    "3155367569301821867326519405336421207498251961314999997901193388809739079012897744887631254739292007"
    "9079474336184847584816275489895017190892204824599487754328163424105552825406129605014330212966409604"
    "2345022792020993821188771907712942838554336194775802398318469748078708796622343248668272179222147264"
    "8796720202642738747343051078071241";

auto main() -> int {
    using beman::big_int::big_int;

    big_int arg_x{};

    constexpr std::span<const char> arg_digits_span(arg_digits, sizeof(arg_digits) - 1);

    const auto fc_result{from_chars(arg_digits_span.data(), arg_digits_span.data() + arg_digits_span.size(), arg_x)};

    static_cast<void>(fc_result);

    const big_int result_sqrt{sqrt(arg_x)};
    const big_int result_cbrt{cbrt(arg_x)};

    const std::string str_result_sqrt{to_string(result_sqrt)};
    const std::string str_result_cbrt{to_string(result_cbrt)};

    const bool result_is_ok{(str_result_sqrt == ctrl_sqrt) && (str_result_cbrt == ctrl_cbrt)};

    std::cout << "result_sqrt:\n" << str_result_sqrt << std::endl;
    std::cout << "\nresult_cbrt:\n" << str_result_cbrt << std::endl;

    std::cout << "\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
