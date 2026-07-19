#ifndef VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_
#define VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_

#include "vibe_othello/board_core/board.h"
#include "vibe_othello/evaluation/pattern_feature_set.h"
#include "vibe_othello/evaluation/pattern_weights.h"
#include "vibe_othello/search/evaluator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vibe_othello::evaluation {

class PatternEvaluator final : public search::Evaluator {
public:
  // The evaluator must outlive the incremental state. Moving the evaluator
  // while an incremental state exists also invalidates the state.
  class IncrementalState {
  public:
    [[nodiscard]] search::Score evaluate() const noexcept;
    void apply_move(board_core::MoveDelta delta) noexcept;
    void undo_move(board_core::MoveDelta delta) noexcept;

    [[nodiscard]] board_core::Color side_to_move() const noexcept {
      return side_to_move_;
    }
    [[nodiscard]] std::uint8_t occupied_count() const noexcept {
      return occupied_count_;
    }
    // Returns the active unique instance count from the most recent apply or undo.
    // Pass transitions report zero.
    [[nodiscard]] std::uint32_t last_touched_instances() const noexcept {
      return last_touched_instances_;
    }

  private:
    friend class PatternEvaluator;
    IncrementalState(const PatternEvaluator* evaluator, board_core::Position position);

    [[nodiscard]] std::uint32_t update_normal_move(board_core::MoveDelta delta,
                                                   board_core::Color mover, int direction) noexcept;
    void rebuild_indices(std::size_t begin, std::size_t end) noexcept;
    void update_absolute_discs(board_core::MoveDelta delta, board_core::Color mover,
                               int direction) noexcept;

    const PatternEvaluator* evaluator_ = nullptr;
    std::vector<std::uint32_t> black_indices_;
    std::vector<std::uint32_t> white_indices_;
    std::vector<std::uint32_t> touched_instances_;
    std::vector<std::uint32_t> touched_generation_;
    std::vector<std::int32_t> pending_black_delta_;
    std::vector<std::int32_t> pending_white_delta_;
    std::uint32_t generation_ = 0;
    std::uint32_t last_touched_instances_ = 0;
    std::uint8_t occupied_count_ = 0;
    board_core::Color side_to_move_ = board_core::Color::black;
    board_core::Bitboard black_discs_ = 0;
    board_core::Bitboard white_discs_ = 0;
    bool maintain_absolute_discs_ = false;
  };

  PatternEvaluator(PatternWeights weights, PatternFeatureSet feature_set);

  search::Score evaluate(const board_core::Position& position) const noexcept override;
  [[nodiscard]] search::Score
  evaluate_reference(const board_core::Position& position) const noexcept;
  [[nodiscard]] IncrementalState make_incremental_state(const board_core::Position& position) const;

private:
  struct InstanceDescriptor {
    std::size_t square_offset = 0;
    std::size_t table_index = 0;
    std::uint32_t pattern_size = 0;
    std::uint8_t pattern_length = 0;
  };

  struct SquareContribution {
    std::uint32_t instance_id = 0;
    std::uint32_t power_of_three = 0;
  };

  struct SquareContributionRange {
    std::size_t offset = 0;
    std::size_t count = 0;
  };

  [[nodiscard]] search::Score evaluate_indices(std::uint8_t occupied_count,
                                               board_core::Color side_to_move,
                                               const std::uint32_t* black_indices,
                                               const std::uint32_t* white_indices) const noexcept;
  [[nodiscard]] search::Score finish_score(std::int64_t scaled_score) const noexcept;
  [[nodiscard]] search::Score flat_weight(std::size_t offset) const noexcept;
  [[nodiscard]] std::size_t active_instance_count(std::uint8_t occupied_count) const noexcept;
  [[nodiscard]] bool has_later_active_instance_change(std::uint8_t occupied_count) const noexcept;

  std::array<std::uint8_t, PatternWeights::kDiscCountEntries> phase_by_disc_count_{};
  std::vector<std::uint32_t> active_instance_counts_;
  std::vector<search::Score> phase_biases_;
  std::uint16_t score_scale_ = 1;
  std::vector<search::Score> flat_weights_;
  std::vector<std::int16_t> compact_flat_weights_;
  std::vector<std::size_t> table_phase_offsets_;
  std::vector<std::size_t> instance_phase_offsets_;
  std::vector<InstanceDescriptor> instances_;
  std::vector<board_core::Square> flat_squares_;
  std::vector<std::uint32_t> flat_powers_of_three_;
  std::array<SquareContributionRange, board_core::kSquareCount> square_ranges_{};
  std::vector<SquareContribution> square_contributions_;
  PatternFeatureSet feature_set_;
};

} // namespace vibe_othello::evaluation

#endif // VIBE_OTHELLO_EVALUATION_PATTERN_EVALUATOR_H_
