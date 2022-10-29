#include "testing_utils.h"

position_t t_utils::GetRandomPositionInField(std::shared_ptr<Field> field, std::shared_ptr<RandomGenerator> ran_gen) {
  return {ran_gen->RandomInt(5, field->lines()-5), ran_gen->RandomInt(5, field->cols()-5)};
}
