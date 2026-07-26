#ifndef MOPPE_PACIOLI_HH
#define MOPPE_PACIOLI_HH

#include <compare>
#include <type_traits>

// A T-account as a numeric representation type: the unreduced pair of
// Ellerman's Pacioli group ("The Mathematics of Double-Entry Bookkeeping",
// 1985). Where a float remembers only a balance, a pacioli value keeps
// debits and credits as separate one-way totals, so an accumulator holds
// its turnover as well as its net. Negation is the swap of sides, so the
// pair is an additive group even over an unsigned carrier -- which is what
// makes it the right ledger for a ring buffer's monotone cursors.
//
// Used as the representation of an mp-units quantity, it turns any
// quantity into a small ledger: postings add, the zero test is the trial
// balance, and two values compare equal exactly when their balances agree
// even if their histories differ.

namespace moppe {
  template <typename T>
  struct pacioli {
    static_assert (std::is_arithmetic_v<T>);

    using value_type = T;

    T debit {};
    T credit {};

    constexpr pacioli () = default;

    // A signed reading splits into the side of the ledger it belongs to.
    constexpr pacioli (T net) {
      if constexpr (std::is_signed_v<T>) {
        if (net < T {}) {
          credit = -net;
          return;
        }
      }
      debit = net;
    }

    constexpr pacioli (T debit, T credit) : debit (debit), credit (credit) {}

    template <typename U>
      requires std::is_convertible_v<U, T>
    constexpr explicit pacioli (const pacioli<U>& other)
        : debit (static_cast<T> (other.debit)),
          credit (static_cast<T> (other.credit)) {}

    // The account's balance. For an unsigned carrier this is modular, which
    // is exactly the contract of monotone ring-buffer cursors.
    constexpr T balance () const {
      return debit - credit;
    }

    // Total activity on both sides: what netting would forget.
    constexpr T turnover () const {
      return debit + credit;
    }

    // The equivalent account with no offsetting activity.
    constexpr pacioli reduced () const {
      const T offset = debit < credit ? debit : credit;
      return { debit - offset, credit - offset };
    }

    // Negation is the swap of sides; no signed carrier is needed.
    constexpr pacioli operator- () const {
      return { credit, debit };
    }

    friend constexpr pacioli operator+ (pacioli a, pacioli b) {
      return { a.debit + b.debit, a.credit + b.credit };
    }

    friend constexpr pacioli operator- (pacioli a, pacioli b) {
      return a + (-b);
    }

    constexpr pacioli& operator+= (pacioli other) {
      return *this = *this + other;
    }

    constexpr pacioli& operator-= (pacioli other) {
      return *this = *this + (-other);
    }

    // The Grothendieck product: multiplication of differences carried out
    // on the unreduced pairs.
    friend constexpr pacioli operator* (pacioli a, pacioli b) {
      return { a.debit * b.debit + a.credit * b.credit,
               a.debit * b.credit + a.credit * b.debit };
    }

    // Division scales the dividend's two sides by the divisor's balance;
    // the divisor's own turnover cannot survive a quotient.
    friend constexpr pacioli operator/ (pacioli a, pacioli b) {
      const T scale = b.balance ();
      if constexpr (std::is_signed_v<T>) {
        if (scale < T {})
          return { a.credit / -scale, a.debit / -scale };
      }
      return { a.debit / scale, a.credit / scale };
    }

    // Two accounts agree when their balances do, said without subtraction:
    // the cross sums compare, so unsigned carriers stay in group law.
    friend constexpr bool operator== (pacioli a, pacioli b) {
      return a.debit + b.credit == b.debit + a.credit;
    }

    friend constexpr auto operator<=> (pacioli a, pacioli b) {
      return a.debit + b.credit <=> b.debit + a.credit;
    }
  };

  template <typename T>
  constexpr pacioli<T> debit_of (T amount) {
    return { amount, T {} };
  }

  template <typename T>
  constexpr pacioli<T> credit_of (T amount) {
    return { T {}, amount };
  }

  // Scaling by a plain number acts on both sides; a negative factor swaps
  // them, so the group action extends the sign-split of construction.
  template <typename T, typename S>
    requires std::is_arithmetic_v<S>
  constexpr auto operator* (pacioli<T> a, S s) {
    using R = std::common_type_t<T, S>;
    if constexpr (std::is_signed_v<S>) {
      if (s < S {})
        return pacioli<R> { static_cast<R> (a.credit * -s),
                            static_cast<R> (a.debit * -s) };
    }
    return pacioli<R> { static_cast<R> (a.debit * s),
                        static_cast<R> (a.credit * s) };
  }

  template <typename T, typename S>
    requires std::is_arithmetic_v<S>
  constexpr auto operator* (S s, pacioli<T> a) {
    return a * s;
  }

  template <typename T, typename S>
    requires std::is_arithmetic_v<S>
  constexpr auto operator/ (pacioli<T> a, S s) {
    using R = std::common_type_t<T, S>;
    if constexpr (std::is_signed_v<S>) {
      if (s < S {})
        return pacioli<R> { static_cast<R> (a.credit / -s),
                            static_cast<R> (a.debit / -s) };
    }
    return pacioli<R> { static_cast<R> (a.debit / s),
                        static_cast<R> (a.credit / s) };
  }
}

template <typename T, typename U>
struct std::common_type<moppe::pacioli<T>, moppe::pacioli<U>> {
  using type = moppe::pacioli<std::common_type_t<T, U>>;
};

template <typename T, typename S>
  requires std::is_arithmetic_v<S>
struct std::common_type<moppe::pacioli<T>, S> {
  using type = moppe::pacioli<std::common_type_t<T, S>>;
};

template <typename S, typename T>
  requires std::is_arithmetic_v<S>
struct std::common_type<S, moppe::pacioli<T>> {
  using type = moppe::pacioli<std::common_type_t<T, S>>;
};

#endif
