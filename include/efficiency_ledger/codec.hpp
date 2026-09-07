#pragma once

#include <cstddef>
#include <string>

#include "efficiency_ledger/entry.hpp"

namespace el {

// Canonical, deterministic binary encoding of a single accounting entry. The
// body is self-delimiting (a 4-byte little-endian length prefix precedes the
// entry bytes). Used by persistence and by the framed transport.
void encode_entry_bytes(const LedgerEntry& e, std::string& out);

// Decode one entry from `in` starting at `offset`, advancing `offset`. Returns
// false on malformed/truncated/bounded-exceeding input.
bool decode_entry_bytes(const std::string& in, std::size_t& offset, LedgerEntry& e);

}  // namespace el
