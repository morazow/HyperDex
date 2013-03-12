// Copyright (c) 2013, Cornell University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of HyperDex nor the names of its contributors may be
//       used to endorse or promote products derived from this software without
//       specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef hyperdex_daemon_datalayer_encodings_h_
#define hyperdex_daemon_datalayer_encodings_h_

// LevelDB
#include <leveldb/slice.h>

// HyperDex
#include "common/ids.h"
#include "daemon/datalayer.h"

namespace hyperdex
{

// Encode the key for objects
void
encode_key(const region_id& ri,
           const e::slice& key,
           std::vector<char>* backing,
           leveldb::Slice* out);
// Decode the key for objects themselves
datalayer::returncode
decode_key(const e::slice& in,
           region_id* ri,
           e::slice* key);

// Encode the value for objects
void
encode_value(const std::vector<e::slice>& attrs,
             uint64_t version,
             std::vector<char>* backing,
             leveldb::Slice* out);
datalayer::returncode
decode_value(const e::slice& in,
             std::vector<e::slice>* attrs,
             uint64_t* version);

// Encode the record of an operation for which we have sent an ACK
#define ACKED_BUF_SIZE (sizeof(uint8_t) + 3 * sizeof(uint64_t))
void
encode_acked(const region_id& ri, /*region we saw an ack for*/
             const region_id& reg_id, /*region of the point leader*/
             uint64_t seq_id,
             char* out);
datalayer::returncode
decode_acked(const e::slice& in,
             region_id* ri, /*region we saw an ack for*/
             region_id* reg_id, /*region of the point leader*/
             uint64_t* seq_id);

// Encode the transfer
#define TRANSFER_BUF_SIZE (sizeof(uint8_t) + 2 * sizeof(uint64_t))
void
encode_transfer(const capture_id& ci,
                uint64_t count,
                char* out);
void
encode_key_value(const e::slice& key,
                 /*pointer to make it optional*/
                 const std::vector<e::slice>* value,
                 uint64_t version,
                 std::vector<char>* backing, /*XXX*/
                 leveldb::Slice* out);
datalayer::returncode
decode_key_value(const e::slice& in,
                 bool* has_value,
                 e::slice* key,
                 std::vector<e::slice>* value,
                 uint64_t* version);

// Encode index elements
void
encode_index(const region_id& ri,
             uint16_t attr,
             std::vector<char>* backing);
void
encode_index(const region_id& ri,
             uint16_t attr,
             hyperdatatype type,
             const e::slice& value,
             std::vector<char>* backing);
void
encode_index(const region_id& ri,
             uint16_t attr,
             hyperdatatype type,
             const e::slice& value,
             const e::slice& key,
             std::vector<char>* backing);
void
bump_index(std::vector<char>* backing);
bool
parse_index_string(const leveldb::Slice& s, e::slice* k);
bool
parse_index_sizeof8(const leveldb::Slice& s, e::slice* k);
bool
parse_object_key(const leveldb::Slice& s, e::slice* k);

datalayer::returncode
create_index_changes(const schema* sc,
                     const subspace* su,
                     const region_id& ri,
                     const e::slice& key,
                     const std::vector<e::slice>* old_value,
                     const std::vector<e::slice>* new_value,
                     leveldb::WriteBatch* updates);

}

#endif // hyperdex_daemon_datalayer_encodings_h_
