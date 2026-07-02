#include "mini_lsm/iterators.h"
#include "mini_lsm/mem_table.h"
#include "mini_lsm/table.h"

namespace mini_lsm {
template class TwoMergeIterator<MemTableIterator, SsTableIterator>;
} // namespace mini_lsm
