#include "mini_lsm/iterators.h"
#include "mini_lsm/mem_table.h"

namespace mini_lsm {
template class MergeIterator<MemTableIterator>;
} // namespace mini_lsm
