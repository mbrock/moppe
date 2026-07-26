#include <moppe/gfx/math.hh>
#include <moppe/pacioli.hh>

#include <tests/test.hh>

#include <cstdint>

using namespace moppe;

namespace {
  using ledger_meters = mp_units::quantity<u::m, pacioli<float>>;
}

MOPPE_TEST (pacioli_is_a_scalar_quantity_representation) {
  static_assert (mp_units::detail::RealScalar<pacioli<float>>);
  static_assert (mp_units::detail::RealScalar<pacioli<std::uint64_t>>);
  static_assert (mp_units::treat_as_floating_point<pacioli<float>>);
  static_assert (!mp_units::treat_as_floating_point<pacioli<std::uint64_t>>);
}

MOPPE_TEST (pacioli_quantity_keeps_turnover_where_a_float_nets) {
  // A column of ground erodes three metres and receives three metres of
  // deposit: a float accumulator would remember nothing.
  ledger_meters column = pacioli<float> {} * u::m;
  column += debit_of (3.0f) * u::m;
  column += credit_of (3.0f) * u::m;

  const pacioli<float> account = column.numerical_value_in (u::m);
  MOPPE_CHECK (account.balance () == 0.0f);
  MOPPE_CHECK (account.turnover () == 6.0f);
  MOPPE_CHECK (column == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_equality_is_the_cross_sum_trial_balance) {
  // Different histories, same balance: equivalent accounts.
  const ledger_meters busy = pacioli<float> { 5.0f, 2.0f } * u::m;
  const ledger_meters quiet = pacioli<float> { 3.0f, 0.0f } * u::m;
  MOPPE_CHECK (busy == quiet);
  MOPPE_CHECK (busy.numerical_value_in (u::m).turnover () !=
               quiet.numerical_value_in (u::m).turnover ());

  // Negation is the swap of sides, and every account cancels its swap.
  MOPPE_CHECK (busy + (-busy) == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_postings_form_zero_sum_transactions) {
  // A transfer posts a debit to one account and a credit to another; the
  // trial balance of the transaction is zero by construction.
  const ledger_meters moved = debit_of (7.0f) * u::m;
  const ledger_meters from = credit_of (7.0f) * u::m;
  MOPPE_CHECK (moved + from == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_scales_through_unit_conversions) {
  const ledger_meters account = pacioli<float> { 1500.0f, 500.0f } * u::m;
  const auto in_km = account.in (u::km);
  const pacioli<float> reading = in_km.numerical_value_in (u::km);
  MOPPE_CHECK_NEAR (reading.balance (), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (reading.turnover (), 2.0f, 1e-6f);
}

MOPPE_TEST (pacioli_multiplication_is_the_grothendieck_product) {
  // (+2 m) * (-3 m) = -6 m^2, carried out on unreduced pairs.
  const auto length = (pacioli<float> { 2.0f, 0.0f } * u::m);
  const auto change = (pacioli<float> { 0.0f, 3.0f } * u::m);
  const auto area = length * change;
  MOPPE_CHECK_NEAR (
    area.numerical_value_in (u::m * u::m).balance (), -6.0f, 1e-6f);
  MOPPE_CHECK (area == pacioli<float> (-6.0f) * (u::m * u::m));
}

MOPPE_TEST (pacioli_of_unsigned_cursors_is_a_ring_buffer_ledger) {
  // head // tail as one unsigned account: enqueue debits, dequeue credits.
  // Ordering and equality use cross sums, so no subtraction ever happens
  // on the unsigned carrier; the balance is the occupancy.
  using cursors = pacioli<std::uint64_t>;
  cursors ring {};
  for (int i = 0; i < 5; ++i)
    ring += debit_of (std::uint64_t { 1 });
  for (int i = 0; i < 2; ++i)
    ring += credit_of (std::uint64_t { 1 });

  MOPPE_CHECK (ring.balance () == 3);
  MOPPE_CHECK (ring.turnover () == 7);
  MOPPE_CHECK (ring.reduced ().debit == 3);
  MOPPE_CHECK (ring.reduced ().credit == 0);

  const cursors capacity = debit_of (std::uint64_t { 8 });
  MOPPE_CHECK (ring < capacity);
  MOPPE_CHECK (ring + debit_of (std::uint64_t { 5 }) == capacity);
}

MOPPE_TEST (pacioli_ordering_compares_balances_across_histories) {
  const pacioli<float> smaller { 4.0f, 1.0f };
  const pacioli<float> larger { 9.0f, 3.0f };
  MOPPE_CHECK (smaller < larger);
  MOPPE_CHECK (larger > smaller);
  MOPPE_CHECK (smaller < pacioli<float> (4.0f));
  MOPPE_CHECK (pacioli<float> (3.0f) == smaller);
}

MOPPE_TEST (pacioli_vectors_are_vector_quantity_representations) {
  using ledger_vec = Vec3T<pacioli<float>>;
  static_assert (mp_units::detail::Vector<ledger_vec>);

  // A walk east three metres and back: the displacement is zero, its
  // magnitude is zero, and yet the eastward component remembers six metres
  // of walking. Odometer and displacement in one value.
  using walk_t =
    mp_units::quantity<mp_units::isq::displacement[u::m], ledger_vec>;
  const walk_t east =
    ledger_vec { debit_of (3.0f), pacioli<float> {}, pacioli<float> {} } * u::m;
  const walk_t walk = east + (-east);

  MOPPE_CHECK (walk == ledger_vec {} * u::m);
  const ledger_vec value = walk.numerical_value_in (u::m);
  MOPPE_CHECK (value[0].balance () == 0.0f);
  MOPPE_CHECK (value[0].turnover () == 6.0f);
  MOPPE_CHECK (walk.magnitude ().numerical_value_in (u::m).balance () == 0.0f);
}

MOPPE_TEST (pacioli_vector_magnitude_norms_the_balances) {
  using ledger_vec = Vec3T<pacioli<float>>;
  // Components with busy histories and balances (3, -4, 0).
  const auto v = ledger_vec { pacioli<float> { 5.0f, 2.0f },
                              pacioli<float> { 1.0f, 5.0f },
                              pacioli<float> { 7.0f, 7.0f } } *
                 mp_units::isq::displacement[u::m];
  const pacioli<float> length = v.magnitude ().numerical_value_in (u::m);
  MOPPE_CHECK_NEAR (length.balance (), 5.0f, 1e-6f);
  MOPPE_CHECK (length.credit == 0.0f);
}

MOPPE_TEST (pacioli_vector_columns_live_in_bundles) {
  // A field of per-site displacement ledgers over the terrain lattice, with
  // the metric Laplacian folding typed influences against pacioli vectors.
  using LedgerFlow = mp_units::quantity<terrain::channel_tangent[mp_units::one],
                                        Vec3T<pacioli<float>>>;
  using LedgerField = spatial::Bundle<terrain::TerrainDomain, LedgerFlow>;

  LedgerField field (
    terrain::TerrainDomain (3, 3, spatial_extent_in_metres (Vec3 (3, 0, 3))));
  auto& column = spatial::get<terrain::channel_tangent> (field);
  column[4] = Vec3T<pacioli<float>> {
    debit_of (2.0f), pacioli<float> {}, credit_of (2.0f)
  } * terrain::channel_tangent[mp_units::one];

  // The field's trial balance: postings across all sites sum by group law.
  LedgerFlow total {};
  for (const auto& entry : column)
    total += entry;
  MOPPE_CHECK (total == column[4]);

  const auto curvature = spatial::laplacian<terrain::channel_tangent> (
    spatial::BundleFocus (field, field.domain ().index (4)));
  const auto reading =
    curvature.numerical_value_in (mp_units::one / (u::m * u::m));
  MOPPE_CHECK_NEAR (reading[0].balance (), -8.0f, 1e-6f);
  MOPPE_CHECK_NEAR (reading[2].balance (), 8.0f, 1e-6f);
}
