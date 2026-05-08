//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "rocksdb/filter_policy.h"
#include "rocksdb/slice.h"
#include "rocksdb/terark_namespace.h"
#include "table/block_based_filter_block.h"
#include "table/full_filter_bits_builder.h"
#include "table/full_filter_block.h"
#include "util/coding.h"
#include "util/hash.h"

#include <arm_sve.h>

namespace TERARKDB_NAMESPACE {

class BlockBasedFilterBlockBuilder;
class FullFilterBlockBuilder;

FullFilterBitsBuilder::FullFilterBitsBuilder(const size_t bits_per_key,
                                             const size_t num_probes)
    : bits_per_key_(bits_per_key), num_probes_(num_probes) {
  assert(bits_per_key_);
}

FullFilterBitsBuilder::~FullFilterBitsBuilder() {}

void FullFilterBitsBuilder::AddKey(const Slice& key) {
  uint32_t hash = BloomHash(key);
  if (hash_entries_.size() == 0 || hash != hash_entries_.back()) {
    hash_entries_.push_back(hash);
  }
}

Slice FullFilterBitsBuilder::Finish(std::unique_ptr<const char[]>* buf) {
  uint32_t total_bits, num_lines;
  char* data = ReserveSpace(static_cast<int>(hash_entries_.size()), &total_bits,
                            &num_lines);
  assert(data);

  if (total_bits != 0 && num_lines != 0) {
    for (auto h : hash_entries_) {
      AddHash(h, data, num_lines, total_bits);
    }
  }
  data[total_bits / 8] = static_cast<char>(num_probes_);
  EncodeFixed32(data + total_bits / 8 + 1, static_cast<uint32_t>(num_lines));

  const char* const_data = data;
  buf->reset(const_data);
  hash_entries_.clear();

  return Slice(data, total_bits / 8 + 5);
}

uint32_t FullFilterBitsBuilder::GetTotalBitsForLocality(uint32_t total_bits) {
  uint32_t num_lines =
      (total_bits + CACHE_LINE_SIZE * 8 - 1) / (CACHE_LINE_SIZE * 8);

  // Make num_lines an odd number to make sure more bits are involved
  // when determining which block.
  if (num_lines % 2 == 0) {
    num_lines++;
  }
  return num_lines * (CACHE_LINE_SIZE * 8);
}

uint32_t FullFilterBitsBuilder::CalculateSpace(const int num_entry,
                                               uint32_t* total_bits,
                                               uint32_t* num_lines) {
  assert(bits_per_key_);
  if (num_entry != 0) {
    uint32_t total_bits_tmp = num_entry * static_cast<uint32_t>(bits_per_key_);

    *total_bits = GetTotalBitsForLocality(total_bits_tmp);
    *num_lines = *total_bits / (CACHE_LINE_SIZE * 8);
    assert(*total_bits > 0 && *total_bits % 8 == 0);
  } else {
    // filter is empty, just leave space for metadata
    *total_bits = 0;
    *num_lines = 0;
  }

  // Reserve space for Filter
  uint32_t sz = *total_bits / 8;
  sz += 5;  // 4 bytes for num_lines, 1 byte for num_probes
  return sz;
}

char* FullFilterBitsBuilder::ReserveSpace(const int num_entry,
                                          uint32_t* total_bits,
                                          uint32_t* num_lines) {
  uint32_t sz = CalculateSpace(num_entry, total_bits, num_lines);
  char* data = new char[sz];
  memset(data, 0, sz);
  return data;
}

int FullFilterBitsBuilder::CalculateNumEntry(const uint32_t space) {
  assert(bits_per_key_);
  assert(space > 0);
  uint32_t dont_care1, dont_care2;
  int high = (int)(space * 8 / bits_per_key_ + 1);
  int low = 1;
  int n = high;
  for (; n >= low; n--) {
    uint32_t sz = CalculateSpace(n, &dont_care1, &dont_care2);
    if (sz <= space) {
      break;
    }
  }
  assert(n < high);  // High should be an overestimation
  return n;
}

inline void FullFilterBitsBuilder::AddHash(uint32_t h, char* data,
                                           uint32_t num_lines,
                                           uint32_t total_bits) {
#ifdef NDEBUG
  (void)total_bits;
#endif
  assert(num_lines > 0 && total_bits > 0);

  const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
  uint32_t b = (h % num_lines) * (CACHE_LINE_SIZE * 8);

  for (uint32_t i = 0; i < num_probes_; ++i) {
    // Since CACHE_LINE_SIZE is defined as 2^n, this line will be optimized
    // to a simple operation by compiler.
    const uint32_t bitpos = b + (h % (CACHE_LINE_SIZE * 8));
    data[bitpos / 8] |= (1 << (bitpos % 8));

    h += delta;
  }
}

namespace {
class FullFilterBitsReader : public FilterBitsReader {
 public:
  explicit FullFilterBitsReader(const Slice& contents)
      : data_(const_cast<char*>(contents.data())),
        data_len_(static_cast<uint32_t>(contents.size())),
        num_probes_(0),
        num_lines_(0),
        log2_cache_line_size_(0) {
    assert(data_);
    GetFilterMeta(contents, &num_probes_, &num_lines_);
    // Sanitize broken parameter
    if (num_lines_ != 0 && (data_len_ - 5) % num_lines_ != 0) {
      num_lines_ = 0;
      num_probes_ = 0;
    } else if (num_lines_ != 0) {
      while (true) {
        uint32_t num_lines_at_curr_cache_size =
            (data_len_ - 5) >> log2_cache_line_size_;
        if (num_lines_at_curr_cache_size == 0) {
          // The cache line size seems not a power of two. It's not supported
          // and indicates a corruption so disable using this filter.
          assert(false);
          num_lines_ = 0;
          num_probes_ = 0;
          break;
        }
        if (num_lines_at_curr_cache_size == num_lines_) {
          break;
        }
        ++log2_cache_line_size_;
      }
    }
  }

  ~FullFilterBitsReader() {}

  virtual bool MayMatch(const Slice& entry) override {
    if (data_len_ <= 5) {  // remain same with original filter
      return false;
    }
    // Other Error params, including a broken filter, regarded as match
    if (num_probes_ == 0 || num_lines_ == 0) return true;
    uint32_t hash = BloomHash(entry);
    return HashMayMatch(hash, Slice(data_, data_len_), num_probes_, num_lines_);
  }

 private:
  // Filter meta data
  char* data_;
  uint32_t data_len_;
  size_t num_probes_;
  uint32_t num_lines_;
  uint32_t log2_cache_line_size_;

  // Get num_probes, and num_lines from filter
  // If filter format broken, set both to 0.
  void GetFilterMeta(const Slice& filter, size_t* num_probes,
                     uint32_t* num_lines);

  // "filter" contains the data appended by a preceding call to
  // FilterBitsBuilder::Finish. This method must return true if the key was
  // passed to FilterBitsBuilder::AddKey. This method may return true or false
  // if the key was not on the list, but it should aim to return false with a
  // high probability.
  //
  // hash: target to be checked
  // filter: the whole filter, including meta data bytes
  // num_probes: number of probes, read before hand
  // num_lines: filter metadata, read before hand
  // Before calling this function, need to ensure the input meta data
  // is valid.
  bool HashMayMatch(const uint32_t& hash, const Slice& filter,
                    const size_t& num_probes, const uint32_t& num_lines);

  // No Copy allowed
  FullFilterBitsReader(const FullFilterBitsReader&);
  void operator=(const FullFilterBitsReader&);
};

void FullFilterBitsReader::GetFilterMeta(const Slice& filter,
                                         size_t* num_probes,
                                         uint32_t* num_lines) {
  uint32_t len = static_cast<uint32_t>(filter.size());
  if (len <= 5) {
    // filter is empty or broken
    *num_probes = 0;
    *num_lines = 0;
    return;
  }

  *num_probes = filter.data()[len - 5];
  *num_lines = DecodeFixed32(filter.data() + len - 4);
}

static inline bool HashMayMatchPrepared(uint32_t h2, int num_probes,
                                        const char *data_at_cache_line) {
  uint32_t h = h2;
  const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
  int rem_probes = num_probes;

  // Pre-compute delta offsets for 8 lanes: {0, delta, 2*delta, ..., 7*delta}
  const svuint32_t delta_offsets =
      svmul_u32_z(svptrue_b32(), svindex_u32(0, 1), svdup_u32(delta));
  const uint32_t delta_8 = delta * 8;

  while (rem_probes > 0) {
    const uint32_t *data_as_uint32 = (const uint32_t *)data_at_cache_line;

    int32_t probes_this_iter = (rem_probes < 8) ? rem_probes : 8;
    svbool_t pg = svwhilelt_b32(0, probes_this_iter);

    // h[j] = h + j*delta  for j = 0..7
    svuint32_t hash_vector = svadd_u32_z(pg, svdup_u32(h), delta_offsets);

    // bitpos = h[j] % (CACHE_LINE_SIZE * 8)  -- uses low bits, matching AddHash
    svuint32_t bitpos = svand_u32_z(
        pg, hash_vector, svdup_u32(static_cast<uint32_t>(CACHE_LINE_SIZE * 8 - 1)));

    // word_address = bitpos >> 5  (9-bit → 4 bits for 16 uint32 words)
    // bit_in_word = bitpos & 31    (5 bits within the uint32)
    svuint32_t word_addresses = svlsr_u32_z(pg, bitpos, svdup_u32(5));
    svuint32_t bit_in_word = svand_u32_z(pg, bitpos, svdup_u32(31));

    // Load 512 bits = 2 SVE regs = 16 uint32 words
    svuint32_t reg0 = svld1_u32(svptrue_b32(), data_as_uint32);
    svuint32_t reg1 = svld1_vnum_u32(svptrue_b32(), data_as_uint32, 1);
    svuint32x2_t all_values = svcreate2(reg0, reg1);
    svuint32_t value_vector = svtbl2_u32(all_values, word_addresses);

    // Check if all target bits are set
    svuint32_t bit_mask = svlsl_u32_z(pg, svdup_u32(1), bit_in_word);
    svuint32_t and_result = svand_u32_z(pg, value_vector, bit_mask);
    svbool_t cmp_result = svcmpne_u32(pg, and_result, bit_mask);

    if (svptest_any(pg, cmp_result)) {
      return false;
    }

    if (rem_probes > 8) {
      h += delta_8;
    }
    rem_probes -= probes_this_iter;
  }
  // for (int i = 0; i < num_probes; ++i) {
  //   // 9-bit address within 512 bit cache line
  //   int bitpos = h % (CACHE_LINE_SIZE * 8);
  //   if ((data_at_cache_line[bitpos >> 3] & (char(1) << (bitpos & 7))) == 0) {
  //     return false;
  //   }
  //   h += delta;
  // }
  return true;
}

bool FullFilterBitsReader::HashMayMatch(const uint32_t& hash,
                                        const Slice& filter,
                                        const size_t& num_probes,
                                        const uint32_t& num_lines) {
  uint32_t len = static_cast<uint32_t>(filter.size());
  if (len <= 5) return false;

  // It is ensured the params are valid before calling it
  assert(num_probes != 0);
  assert(num_lines != 0 && (len - 5) % num_lines == 0);
  const char* data = filter.data();

  uint32_t bytes_to_cache_line = (hash % num_lines) << log2_cache_line_size_;
  PREFETCH(&data[bytes_to_cache_line], 0 /* rw */, 1 /* locality */);
  PREFETCH(&data[bytes_to_cache_line + (1 << log2_cache_line_size_) - 1],
           0 /* rw */, 1 /* locality */);

  return HashMayMatchPrepared(hash, static_cast<int>(num_probes),
                              data + bytes_to_cache_line);
}

// An implementation of filter policy
class BloomFilterPolicy : public FilterPolicy {
 public:
  explicit BloomFilterPolicy(int bits_per_key, bool use_block_based_builder)
      : bits_per_key_(bits_per_key),
        hash_func_(BloomHash),
        use_block_based_builder_(use_block_based_builder) {
    initialize();
  }

  ~BloomFilterPolicy() {}

  virtual const char* Name() const override {
    return "rocksdb.BuiltinBloomFilter";
  }

  virtual void CreateFilter(const Slice* keys, int n,
                            std::string* dst) const override {
    // Compute bloom filter size (in both bits and bytes)
    size_t bits = n * bits_per_key_;

    // For small n, we can see a very high false positive rate.  Fix it
    // by enforcing a minimum bloom filter length.
    if (bits < 64) bits = 64;

    size_t bytes = (bits + 7) / 8;
    bits = bytes * 8;

    const size_t init_size = dst->size();
    dst->resize(init_size + bytes, 0);
    dst->push_back(static_cast<char>(num_probes_));  // Remember # of probes
    char* array = &(*dst)[init_size];
    for (size_t i = 0; i < (size_t)n; i++) {
      // Use double-hashing to generate a sequence of hash values.
      // See analysis in [Kirsch,Mitzenmacher 2006].
      uint32_t h = hash_func_(keys[i]);
      const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
      for (size_t j = 0; j < num_probes_; j++) {
        const uint32_t bitpos = h % bits;
        array[bitpos / 8] |= (1 << (bitpos % 8));
        h += delta;
      }
    }
  }

  virtual bool KeyMayMatch(const Slice& key,
                           const Slice& bloom_filter) const override {
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char* array = bloom_filter.data();
    const size_t bits = (len - 1) * 8;

    // Use the encoded k so that we can read filters generated by
    // bloom filters created using different parameters.
    const size_t k = array[len - 1];
    if (k > 30) {
      // Reserved for potentially new encodings for short bloom filters.
      // Consider it a match.
      return true;
    }

    uint32_t h = hash_func_(key);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k; j++) {
      const uint32_t bitpos = h % bits;
      if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) return false;
      h += delta;
    }
    return true;
  }

  virtual FilterBitsBuilder* GetFilterBitsBuilder() const override {
    if (use_block_based_builder_) {
      return nullptr;
    }

    return new FullFilterBitsBuilder(bits_per_key_, num_probes_);
  }

  virtual FilterBitsReader* GetFilterBitsReader(
      const Slice& contents) const override {
    return new FullFilterBitsReader(contents);
  }

  // If choose to use block based builder
  bool UseBlockBasedBuilder() { return use_block_based_builder_; }

 private:
  size_t bits_per_key_;
  size_t num_probes_;
  uint32_t (*hash_func_)(const Slice& key);

  const bool use_block_based_builder_;

  void initialize() {
    // We intentionally round down to reduce probing cost a little bit
    num_probes_ = static_cast<size_t>(bits_per_key_ * 0.69);  // 0.69 =~ ln(2)
    if (num_probes_ < 1) num_probes_ = 1;
    if (num_probes_ > 30) num_probes_ = 30;
  }
};

}  // namespace

const FilterPolicy* NewBloomFilterPolicy(int bits_per_key,
                                         bool use_block_based_builder) {
  return new BloomFilterPolicy(bits_per_key, use_block_based_builder);
}

}  // namespace TERARKDB_NAMESPACE
