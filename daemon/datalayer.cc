// Copyright (c) 2012, Cornell University
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

#define __STDC_LIMIT_MACROS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// POSIX
#include <signal.h>

// STL
#include <sstream>
#include <string>

// Google Log
#include <glog/logging.h>

// LevelDB
#include <leveldb/write_batch.h>
#include <leveldb/filter_policy.h>

// e
#include <e/endian.h>

// HyperDex
#include "common/macros.h"
#include "common/range_searches.h"
#include "common/serialization.h"
#include "daemon/daemon.h"
#include "daemon/datalayer.h"
#include "daemon/datalayer_encodings.h"
#include "datatypes/apply.h"
#include "datatypes/microerror.h"

// ASSUME:  all keys put into leveldb have a first byte without the high bit set

using std::tr1::placeholders::_1;
using hyperdex::datalayer;
using hyperdex::leveldb_snapshot_ptr;
using hyperdex::reconfigure_returncode;

datalayer :: datalayer(daemon* d)
    : m_daemon(d)
    , m_db()
    , m_counters()
    , m_cleaner(std::tr1::bind(&datalayer::cleaner, this))
    , m_block_cleaner()
    , m_wakeup_cleaner(&m_block_cleaner)
    , m_wakeup_reconfigurer(&m_block_cleaner)
    , m_need_cleaning(false)
    , m_shutdown(true)
    , m_need_pause(false)
    , m_paused(false)
    , m_state_transfer_captures()
{
}

datalayer :: ~datalayer() throw ()
{
    shutdown();
}

bool
datalayer :: setup(const po6::pathname& path,
                   bool* saved,
                   server_id* saved_us,
                   po6::net::location* saved_bind_to,
                   po6::net::hostname* saved_coordinator)
{
    leveldb::Options opts;
    opts.write_buffer_size = 64ULL * 1024ULL * 1024ULL;
    opts.create_if_missing = true;
    opts.filter_policy = leveldb::NewBloomFilterPolicy(10);
    std::string name(path.get());
    leveldb::DB* tmp_db;
    leveldb::Status st = leveldb::DB::Open(opts, name, &tmp_db);

    if (!st.ok())
    {
        LOG(ERROR) << "could not open LevelDB: " << st.ToString();
        return false;
    }

    m_db.reset(tmp_db);
    leveldb::ReadOptions ropts;
    ropts.fill_cache = true;
    ropts.verify_checksums = true;

    leveldb::Slice rk("hyperdex", 8);
    std::string rbacking;
    st = m_db->Get(ropts, rk, &rbacking);
    bool first_time = false;

    if (st.ok())
    {
        first_time = false;

        if (rbacking != PACKAGE_VERSION &&
            rbacking != "1.0.rc1" &&
            rbacking != "1.0.rc2")
        {
            LOG(ERROR) << "could not restore from LevelDB because "
                       << "the existing data was created by "
                       << "HyperDex " << rbacking << " but "
                       << "this is version " << PACKAGE_VERSION;
            return false;
        }
    }
    else if (st.IsNotFound())
    {
        first_time = true;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "could not restore from LevelDB because of corruption:  "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "could not restore from LevelDB because of an IO error:  "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "could not restore from LevelDB because it returned an "
                   << "unknown error that we don't know how to handle:  "
                   << st.ToString();
        return false;
    }

    leveldb::Slice sk("state", 5);
    std::string sbacking;
    st = m_db->Get(ropts, sk, &sbacking);

    if (st.ok())
    {
        if (first_time)
        {
            LOG(ERROR) << "could not restore from LevelDB because a previous "
                       << "execution crashed and the database was tampered with; "
                       << "you're on your own with this one";
            return false;
        }
    }
    else if (st.IsNotFound())
    {
        if (!first_time)
        {
            LOG(ERROR) << "could not restore from LevelDB because a previous "
                       << "execution crashed; run the recovery program and try again";
            return false;
        }
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "could not restore from LevelDB because of corruption:  "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "could not restore from LevelDB because of an IO error:  "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "could not restore from LevelDB because it returned an "
                   << "unknown error that we don't know how to handle:  "
                   << st.ToString();
        return false;
    }

    {
        po6::threads::mutex::hold hold(&m_block_cleaner);
        m_cleaner.start();
        m_shutdown = false;
    }

    if (first_time)
    {
        *saved = false;
        return true;
    }

    uint64_t us;
    *saved = true;
    e::unpacker up(sbacking.data(), sbacking.size());
    up = up >> us >> *saved_bind_to >> *saved_coordinator;
    *saved_us = server_id(us);

    if (up.error())
    {
        LOG(ERROR) << "could not restore from LevelDB because a previous "
                   << "execution saved invalid state; run the recovery program and try again";
        return false;
    }

    return true;
}

void
datalayer :: teardown()
{
    shutdown();
}

bool
datalayer :: initialize()
{
    leveldb::WriteOptions wopts;
    wopts.sync = true;
    leveldb::Status st = m_db->Put(wopts, leveldb::Slice("hyperdex", 8),
                                   leveldb::Slice(PACKAGE_VERSION, strlen(PACKAGE_VERSION)));

    if (st.ok())
    {
        return true;
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "could not initialize LevelDB because Put returned NotFound:  "
                   << st.ToString();
        return false;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "could not initialize LevelDB because of corruption:  "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "could not initialize LevelDB because of an IO error:  "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "could not initialize LevelDB because it returned an "
                   << "unknown error that we don't know how to handle:  "
                   << st.ToString();
        return false;
    }
}

bool
datalayer :: save_state(const server_id& us,
                        const po6::net::location& bind_to,
                        const po6::net::hostname& coordinator)
{
    leveldb::WriteOptions wopts;
    wopts.sync = true;
    leveldb::Status st = m_db->Put(wopts,
                                   leveldb::Slice("dirty", 5),
                                   leveldb::Slice("", 0));

    if (st.ok())
    {
        // Yay
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "could not set dirty bit: "
                   << st.ToString();
        return false;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: "
                   << "could not set dirty bit: "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: "
                   << "could not set dirty bit: "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't "
                   << "know how to handle: could not set dirty bit";
        return false;
    }

    size_t sz = sizeof(uint64_t)
              + pack_size(bind_to)
              + pack_size(coordinator);
    std::auto_ptr<e::buffer> state(e::buffer::create(sz));
    *state << us << bind_to << coordinator;
    st = m_db->Put(wopts, leveldb::Slice("state", 5),
                   leveldb::Slice(reinterpret_cast<const char*>(state->data()), state->size()));

    if (st.ok())
    {
        return true;
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "could not save state: "
                   << st.ToString();
        return false;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: "
                   << "could not save state: "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: "
                   << "could not save state: "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't "
                   << "know how to handle: could not save state";
        return false;
    }
}

bool
datalayer :: clear_dirty()
{
    leveldb::WriteOptions wopts;
    wopts.sync = true;
    leveldb::Slice key("dirty", 5);
    leveldb::Status st = m_db->Delete(wopts, key);

    if (st.ok() || st.IsNotFound())
    {
        return true;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: "
                   << "could not clear dirty bit: "
                   << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: "
                   << "could not clear dirty bit: "
                   << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't "
                   << "know how to handle: could not clear dirty bit";
        return false;
    }
}

void
datalayer :: pause()
{
    po6::threads::mutex::hold hold(&m_block_cleaner);
    assert(!m_need_pause);
    m_need_pause = true;
}

void
datalayer :: unpause()
{
    po6::threads::mutex::hold hold(&m_block_cleaner);
    assert(m_need_pause);
    m_wakeup_cleaner.broadcast();
    m_need_pause = false;
    m_need_cleaning = true;
}

void
datalayer :: reconfigure(const configuration&,
                         const configuration& new_config,
                         const server_id& us)
{
    {
        po6::threads::mutex::hold hold(&m_block_cleaner);
        assert(m_need_pause);

        while (!m_paused)
        {
            m_wakeup_reconfigurer.wait();
        }
    }

    std::vector<capture> captures;
    new_config.captures(&captures);
    std::vector<region_id> regions;
    regions.reserve(captures.size());

    for (size_t i = 0; i < captures.size(); ++i)
    {
        if (new_config.get_virtual(captures[i].rid, us) != virtual_server_id())
        {
            regions.push_back(captures[i].rid);
        }
    }

    std::sort(regions.begin(), regions.end());
    m_counters.adopt(regions);
}

datalayer::returncode
datalayer :: get(const region_id& ri,
                 const e::slice& key,
                 std::vector<e::slice>* value,
                 uint64_t* version,
                 reference* ref)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = true;
    opts.verify_checksums = true;
    std::vector<char> kbacking;
    leveldb::Slice lkey;
    encode_key(ri, key, &kbacking, &lkey);
    leveldb::Status st = m_db->Get(opts, lkey, &ref->m_backing);

    if (st.ok())
    {
        e::slice v(ref->m_backing.data(), ref->m_backing.size());
        return decode_value(v, value, version);
    }
    else if (st.IsNotFound())
    {
        return NOT_FOUND;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: del(const region_id& ri,
                 const region_id& reg_id,
                 uint64_t seq_id,
                 const e::slice& key,
                 const std::vector<e::slice>& old_value)
{
    leveldb::WriteBatch updates;
    std::vector<char> backing1;
    std::vector<char> backing2;

    // peform the "del" of the object we want to store
    leveldb::Slice lkey;
    encode_key(ri, key, &backing1, &lkey);
    updates.Delete(lkey);

    // apply the index operations
    const schema* sc = m_daemon->m_config.get_schema(ri);
    const subspace* su = m_daemon->m_config.get_subspace(ri);
    returncode rc = create_index_changes(sc, su, ri, key, &old_value, NULL, &updates);

    if (rc != SUCCESS)
    {
        return rc;
    }

    // Mark acked as part of this batch write
    if (seq_id != 0)
    {
        char abacking[ACKED_BUF_SIZE];
        seq_id = UINT64_MAX - seq_id;
        encode_acked(ri, reg_id, seq_id, abacking);
        leveldb::Slice akey(abacking, ACKED_BUF_SIZE);
        leveldb::Slice aval("", 0);
        updates.Put(akey, aval);
    }

    uint64_t count;

    // If this is a captured region, then we must log this transfer
    if (m_counters.lookup(ri, &count))
    {
        char tbacking[TRANSFER_BUF_SIZE];
        capture_id cid = m_daemon->m_config.capture_for(ri);
        assert(cid != capture_id());
        leveldb::Slice tkey(tbacking, TRANSFER_BUF_SIZE);
        leveldb::Slice tval;
        encode_transfer(cid, count, tbacking);
        encode_key_value(key, NULL, 0, &backing2, &tval);
        updates.Put(tkey, tval);
    }

    // Perform the write
    leveldb::WriteOptions opts;
    opts.sync = false;
    leveldb::Status st = m_db->Write(opts, &updates);

    if (st.ok())
    {
        return SUCCESS;
    }
    else if (st.IsNotFound())
    {
        return NOT_FOUND;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: put(const region_id& ri,
                 const region_id& reg_id,
                 uint64_t seq_id,
                 const e::slice& key,
                 const std::vector<e::slice>& new_value,
                 uint64_t version)
{
    leveldb::WriteBatch updates;
    std::vector<char> backing1;
    std::vector<char> backing2;

    // peform the "put" of the object we want to store
    leveldb::Slice lkey;
    leveldb::Slice lval;
    encode_key(ri, key, &backing1, &lkey);
    encode_value(new_value, version, &backing2, &lval);
    updates.Put(lkey, lval);

    // apply the index operations
    const schema* sc = m_daemon->m_config.get_schema(ri);
    const subspace* su = m_daemon->m_config.get_subspace(ri);
    returncode rc = create_index_changes(sc, su, ri, key, NULL, &new_value, &updates);

    if (rc != SUCCESS)
    {
        return rc;
    }

    // Mark acked as part of this batch write
    if (seq_id != 0)
    {
        char abacking[ACKED_BUF_SIZE];
        seq_id = UINT64_MAX - seq_id;
        encode_acked(ri, reg_id, seq_id, abacking);
        leveldb::Slice akey(abacking, ACKED_BUF_SIZE);
        leveldb::Slice aval("", 0);
        updates.Put(akey, aval);
    }

    uint64_t count;

    // If this is a captured region, then we must log this transfer
    if (m_counters.lookup(ri, &count))
    {
        char tbacking[TRANSFER_BUF_SIZE];
        capture_id cid = m_daemon->m_config.capture_for(ri);
        assert(cid != capture_id());
        leveldb::Slice tkey(tbacking, TRANSFER_BUF_SIZE);
        leveldb::Slice tval;
        encode_transfer(cid, count, tbacking);
        encode_key_value(key, &new_value, version, &backing2, &tval);
        updates.Put(tkey, tval);
    }

    // Perform the write
    leveldb::WriteOptions opts;
    opts.sync = false;
    leveldb::Status st = m_db->Write(opts, &updates);

    if (st.ok())
    {
        return SUCCESS;
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "put returned NOT_FOUND at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return NOT_FOUND;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: overput(const region_id& ri,
                     const region_id& reg_id,
                     uint64_t seq_id,
                     const e::slice& key,
                     const std::vector<e::slice>& old_value,
                     const std::vector<e::slice>& new_value,
                     uint64_t version)
{
    leveldb::WriteBatch updates;
    std::vector<char> backing1;
    std::vector<char> backing2;

    // peform the "put" of the object we want to store
    leveldb::Slice lkey;
    leveldb::Slice lval;
    encode_key(ri, key, &backing1, &lkey);
    encode_value(new_value, version, &backing2, &lval);
    updates.Put(lkey, lval);

    // apply the index operations
    const schema* sc = m_daemon->m_config.get_schema(ri);
    const subspace* su = m_daemon->m_config.get_subspace(ri);
    returncode rc = create_index_changes(sc, su, ri, key, &old_value, &new_value, &updates);

    if (rc != SUCCESS)
    {
        return rc;
    }

    // Mark acked as part of this batch write
    if (seq_id != 0)
    {
        char abacking[ACKED_BUF_SIZE];
        seq_id = UINT64_MAX - seq_id;
        encode_acked(ri, reg_id, seq_id, abacking);
        leveldb::Slice akey(abacking, ACKED_BUF_SIZE);
        leveldb::Slice aval("", 0);
        updates.Put(akey, aval);
    }

    uint64_t count;

    // If this is a captured region, then we must log this transfer
    if (m_counters.lookup(ri, &count))
    {
        char tbacking[TRANSFER_BUF_SIZE];
        capture_id cid = m_daemon->m_config.capture_for(ri);
        assert(cid != capture_id());
        leveldb::Slice tkey(tbacking, TRANSFER_BUF_SIZE);
        leveldb::Slice tval;
        encode_transfer(cid, count, tbacking);
        encode_key_value(key, &new_value, version, &backing2, &tval);
        updates.Put(tkey, tval);
    }

    // Perform the write
    leveldb::WriteOptions opts;
    opts.sync = false;
    leveldb::Status st = m_db->Write(opts, &updates);

    if (st.ok())
    {
        return SUCCESS;
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "overput returned NOT_FOUND at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return NOT_FOUND;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: uncertain_del(const region_id& ri,
                           const e::slice& key)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = false;
    opts.verify_checksums = true;
    leveldb::Slice lkey;
    std::vector<char> kbacking;
    encode_key(ri, key, &kbacking, &lkey);
    std::string ref;
    leveldb::Status st = m_db->Get(opts, lkey, &ref);

    if (st.ok())
    {
        const schema* sc = m_daemon->m_config.get_schema(ri);
        std::vector<e::slice> old_value;
        uint64_t old_version;
        returncode rc = decode_value(e::slice(ref.data(), ref.size()),
                                     &old_value, &old_version);

        if (rc != SUCCESS)
        {
            return rc;
        }

        if (old_value.size() + 1 != sc->attrs_sz)
        {
            return BAD_ENCODING;
        }

        return del(ri, region_id(), 0, key, old_value);
    }
    else if (st.IsNotFound())
    {
        return SUCCESS;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: uncertain_put(const region_id& ri,
                           const e::slice& key,
                           const std::vector<e::slice>& new_value,
                           uint64_t version)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = false;
    opts.verify_checksums = true;
    leveldb::Slice lkey;
    std::vector<char> kbacking;
    encode_key(ri, key, &kbacking, &lkey);
    std::string ref;
    leveldb::Status st = m_db->Get(opts, lkey, &ref);

    if (st.ok())
    {
        const schema* sc = m_daemon->m_config.get_schema(ri);
        std::vector<e::slice> old_value;
        uint64_t old_version;
        returncode rc = decode_value(e::slice(ref.data(), ref.size()),
                                     &old_value, &old_version);

        if (rc != SUCCESS)
        {
            return rc;
        }

        if (old_value.size() + 1 != sc->attrs_sz)
        {
            return BAD_ENCODING;
        }

        return overput(ri, region_id(), 0, key, old_value, new_value, version);
    }
    else if (st.IsNotFound())
    {
        return put(ri, region_id(), 0, key, new_value, version);
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " key=0x" << key.hex() << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

datalayer::returncode
datalayer :: make_snapshot(const region_id& ri,
                           const schema& sc,
                           const std::vector<attribute_check>* checks,
                           snapshot* snap,
                           std::ostringstream* ostr)
{
    snap->m_dl = this;
    snap->m_snap.reset(m_db, m_db->GetSnapshot());
    snap->m_checks = checks;
    snap->m_ri = ri;
    snap->m_ostr = ostr;
    std::vector<range> ranges;

    if (!range_searches(*checks, &ranges))
    {
        return BAD_SEARCH;
    }

    if (ostr) *ostr << " converted " << checks->size() << " checks to " << ranges.size() << " ranges\n";

    char* ptr;
    std::vector<leveldb::Range> level_ranges;
    std::vector<bool (*)(const leveldb::Slice& in, e::slice* out)> parsers;

    // For each range, setup a leveldb range using encoded values
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        if (ostr) *ostr << " considering attr " << ranges[i].attr << " Range("
                        << ranges[i].start.hex() << ", " << ranges[i].end.hex() << " " << ranges[i].type << " "
                        << (ranges[i].has_start ? "[" : "<") << "-" << (ranges[i].has_end ? "]" : ">")
                        << " " << (ranges[i].invalid ? "invalid" : "valid") << "\n";

        if (ranges[i].attr >= sc.attrs_sz ||
            sc.attrs[ranges[i].attr].type != ranges[i].type)
        {
            return BAD_SEARCH;
        }

        bool (*parse)(const leveldb::Slice& in, e::slice* out);

        // XXX sometime in the future we could support efficient range search on
        // keys.  Today is not that day.  Tomorrow doesn't look good either.
        if (ranges[i].attr == 0)
        {
            continue;
        }

        else if (ranges[i].type == HYPERDATATYPE_STRING)
        {
            parse = &parse_index_string;
        }
        else if (ranges[i].type == HYPERDATATYPE_INT64)
        {
            parse = &parse_index_sizeof8;
        }
        else if (ranges[i].type == HYPERDATATYPE_FLOAT)
        {
            parse = &parse_index_sizeof8;
        }
        else
        {
            continue;
        }

        if (ranges[i].has_start)
        {
            snap->m_backing.push_back(std::vector<char>());
            encode_index(ri, ranges[i].attr, ranges[i].type, ranges[i].start, &snap->m_backing.back());
        }
        else
        {
            snap->m_backing.push_back(std::vector<char>());
            encode_index(ri, ranges[i].attr, &snap->m_backing.back());
        }

        leveldb::Slice start(&snap->m_backing.back()[0], snap->m_backing.back().size());

        if (ranges[i].has_end && ranges[i].type == HYPERDATATYPE_STRING)
        {
            e::slice new_end = e::slice(ranges[i].end.data(), std::min(ranges[i].end.size(), ranges[i].end.size()));
            snap->m_backing.push_back(std::vector<char>());
            encode_index(ri, ranges[i].attr, ranges[i].type, new_end, &snap->m_backing.back());
            bump_index(&snap->m_backing.back());
        }
        else if (ranges[i].has_end)
        {
            snap->m_backing.push_back(std::vector<char>());
            encode_index(ri, ranges[i].attr, ranges[i].type, ranges[i].end, &snap->m_backing.back());
            bump_index(&snap->m_backing.back());
        }
        else
        {
            snap->m_backing.push_back(std::vector<char>());
            encode_index(ri, ranges[i].attr + 1, &snap->m_backing.back());
        }

        leveldb::Slice limit(&snap->m_backing.back()[0], snap->m_backing.back().size());
        level_ranges.push_back(leveldb::Range(start, limit));
        parsers.push_back(parse);
    }

    // Add to level_ranges the size of the object range for the region itself
    // excluding indices
    level_ranges.push_back(leveldb::Range());
    snap->m_backing.push_back(std::vector<char>());
    snap->m_backing.back().resize(sizeof(uint8_t) + sizeof(uint64_t));
    ptr = &snap->m_backing.back()[0];
    ptr = e::pack8be('o', ptr);
    ptr = e::pack64be(ri.get(), ptr);
    level_ranges.back().start = leveldb::Slice(&snap->m_backing.back()[0],
                                               snap->m_backing.back().size());
    snap->m_backing.push_back(snap->m_backing.back());
    bump_index(&snap->m_backing.back());
    level_ranges.back().limit = leveldb::Slice(&snap->m_backing.back()[0],
                                               snap->m_backing.back().size());

    // Fetch from leveldb the approximate space usage of each computed range
    std::vector<uint64_t> sizes(level_ranges.size());
    m_db->GetApproximateSizes(&level_ranges.front(), level_ranges.size(), &sizes.front());

    if (ostr)
    {
        for (size_t i = 0; i < level_ranges.size(); ++i)
        {
            *ostr << " index Range(" << e::slice(level_ranges[i].start.data(), level_ranges[i].start.size()).hex()
                  << ", " << e::slice(level_ranges[i].limit.data(), level_ranges[i].limit.size()).hex()
                  << " occupies " << sizes[i] << " bytes\n";
        }
    }

    // the size of all objects in the region of the search
    uint64_t object_disk_space = sizes.back();
    if (ostr) *ostr << " objects for " << ri << " occupies " << object_disk_space << " bytes\n";
    leveldb::Range object_range = level_ranges.back();
    level_ranges.pop_back();
    sizes.pop_back();
    assert(parsers.size() == sizes.size());
    assert(parsers.size() == level_ranges.size());

    // Figure out the smallest indices
    std::vector<std::pair<uint64_t, size_t> > size_idxs;

    for (size_t i = 0; i < sizes.size(); ++i)
    {
        size_idxs.push_back(std::make_pair(sizes[i], i));
    }

    std::sort(size_idxs.begin(), size_idxs.end());

    // Figure out the plan of attack.  Three options:
    // 1.  Scan all objects (idx = 0)
    // 2.  Use the least costly index (idx = 1)
    // 3.  Use the least costly index plus bloom filters pulled from other
    //     low-cost indices. (idx > 1)
    size_t idx = 0;
    size_t sum = 0;

    while (idx < size_idxs.size())
    {
        if (sum + size_idxs[idx].first < object_disk_space / 4. &&
            (idx == 0 || size_idxs[idx - 1].first * 10 > size_idxs[idx].first))

        {
            if (ostr) *ostr << " next smallest index is " << size_idxs[idx].second << ":  small enough\n";
            sum += size_idxs[idx].first;
            ++idx;
        }
        else
        {
            if (ostr) *ostr << " next smallest index is " << size_idxs[idx].second << ":  too big\n";
            break;
        }
    }

    if (idx == 0)
    {
        if (ostr) *ostr << " choosing to just enumerate all objects\n";
        snap->m_range = object_range;
        snap->m_parse = &parse_object_key;
    }
    else
    {
        size_t tidx = size_idxs[0].second;
        if (ostr) *ostr << " choosing to use index " << tidx << " as the primary\n";
        snap->m_range = level_ranges[tidx];
        snap->m_parse = parsers[tidx];
    }

    // Create iterator
    leveldb::ReadOptions opts;
    opts.fill_cache = false;
    opts.verify_checksums = false;
    opts.snapshot = snap->m_snap.get();
    snap->m_iter.reset(snap->m_snap, m_db->NewIterator(opts));
    snap->m_iter->Seek(snap->m_range.start);
    return SUCCESS;
}

leveldb_snapshot_ptr
datalayer :: make_raw_snapshot()
{
    return leveldb_snapshot_ptr(m_db, m_db->GetSnapshot());
}

void
datalayer :: make_region_iterator(region_iterator* riter,
                                  leveldb_snapshot_ptr snap,
                                  const region_id& ri)
{
    riter->m_dl = this;
    riter->m_snap = snap;
    riter->m_region = ri;
    leveldb::ReadOptions opts;
    opts.fill_cache = true;
    opts.verify_checksums = true;
    opts.snapshot = riter->m_snap.get();
    riter->m_iter.reset(riter->m_snap, m_db->NewIterator(opts));
    char backing[sizeof(uint8_t) + sizeof(uint64_t)];
    char* ptr = backing;
    ptr = e::pack8be('o', ptr);
    ptr = e::pack64be(riter->m_region.get(), ptr);
    riter->m_iter->Seek(leveldb::Slice(backing, sizeof(uint8_t) + sizeof(uint64_t)));
}

datalayer::returncode
datalayer :: get_transfer(const region_id& ri,
                          uint64_t seq_no,
                          bool* has_value,
                          e::slice* key,
                          std::vector<e::slice>* value,
                          uint64_t* version,
                          reference* ref)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = true;
    opts.verify_checksums = true;
    char tbacking[TRANSFER_BUF_SIZE];
    capture_id cid = m_daemon->m_config.capture_for(ri);
    assert(cid != capture_id());
    leveldb::Slice lkey(tbacking, TRANSFER_BUF_SIZE);
    encode_transfer(cid, seq_no, tbacking);
    leveldb::Status st = m_db->Get(opts, lkey, &ref->m_backing);

    if (st.ok())
    {
        e::slice v(ref->m_backing.data(), ref->m_backing.size());
        return decode_key_value(v, has_value, key, value, version);
    }
    else if (st.IsNotFound())
    {
        return NOT_FOUND;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << ri
                   << " seq_no=" << seq_no << " desc=" << st.ToString();
        return CORRUPTION;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << ri
                   << " seq_no=" << seq_no << " desc=" << st.ToString();
        return IO_ERROR;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return LEVELDB_ERROR;
    }
}

bool
datalayer :: check_acked(const region_id& ri,
                         const region_id& reg_id,
                         uint64_t seq_id)
{
    // make it so that increasing seq_ids are ordered in reverse in the KVS
    seq_id = UINT64_MAX - seq_id;
    leveldb::ReadOptions opts;
    opts.fill_cache = true;
    opts.verify_checksums = true;
    char abacking[ACKED_BUF_SIZE];
    encode_acked(ri, reg_id, seq_id, abacking);
    leveldb::Slice akey(abacking, ACKED_BUF_SIZE);
    std::string val;
    leveldb::Status st = m_db->Get(opts, akey, &val);

    if (st.ok())
    {
        return true;
    }
    else if (st.IsNotFound())
    {
        return false;
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << reg_id
                   << " seq_id=" << seq_id << " desc=" << st.ToString();
        return false;
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << reg_id
                   << " seq_id=" << seq_id << " desc=" << st.ToString();
        return false;
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
        return false;
    }
}

void
datalayer :: mark_acked(const region_id& ri,
                        const region_id& reg_id,
                        uint64_t seq_id)
{
    // make it so that increasing seq_ids are ordered in reverse in the KVS
    seq_id = UINT64_MAX - seq_id;
    leveldb::WriteOptions opts;
    opts.sync = false;
    char abacking[ACKED_BUF_SIZE];
    encode_acked(ri, reg_id, seq_id, abacking);
    leveldb::Slice akey(abacking, ACKED_BUF_SIZE);
    leveldb::Slice val("", 0);
    leveldb::Status st = m_db->Put(opts, akey, val);

    if (st.ok())
    {
        // Yay!
    }
    else if (st.IsNotFound())
    {
        LOG(ERROR) << "mark_acked returned NOT_FOUND at the disk layer: region=" << reg_id
                   << " seq_id=" << seq_id << " desc=" << st.ToString();
    }
    else if (st.IsCorruption())
    {
        LOG(ERROR) << "corruption at the disk layer: region=" << reg_id
                   << " seq_id=" << seq_id << " desc=" << st.ToString();
    }
    else if (st.IsIOError())
    {
        LOG(ERROR) << "IO error at the disk layer: region=" << reg_id
                   << " seq_id=" << seq_id << " desc=" << st.ToString();
    }
    else
    {
        LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
    }
}

void
datalayer :: max_seq_id(const region_id& reg_id,
                        uint64_t* seq_id)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = false;
    opts.verify_checksums = true;
    opts.snapshot = NULL;
    std::auto_ptr<leveldb::Iterator> it(m_db->NewIterator(opts));
    char abacking[ACKED_BUF_SIZE];
    encode_acked(reg_id, reg_id, 0, abacking);
    leveldb::Slice key(abacking, ACKED_BUF_SIZE);
    it->Seek(key);

    if (!it->Valid())
    {
        *seq_id = 0;
        return;
    }

    key = it->key();
    region_id tmp_ri;
    region_id tmp_reg_id;
    uint64_t tmp_seq_id;
    datalayer::returncode rc = decode_acked(e::slice(key.data(), key.size()),
                                            &tmp_ri, &tmp_reg_id, &tmp_seq_id);

    if (rc != SUCCESS || tmp_ri != reg_id || tmp_reg_id != reg_id)
    {
        *seq_id = 0;
        return;
    }

    *seq_id = UINT64_MAX - tmp_seq_id;
}

void
datalayer :: clear_acked(const region_id& reg_id,
                         uint64_t seq_id)
{
    leveldb::ReadOptions opts;
    opts.fill_cache = false;
    opts.verify_checksums = true;
    opts.snapshot = NULL;
    std::auto_ptr<leveldb::Iterator> it(m_db->NewIterator(opts));
    char abacking[ACKED_BUF_SIZE];
    encode_acked(region_id(0), reg_id, 0, abacking);
    it->Seek(leveldb::Slice(abacking, ACKED_BUF_SIZE));
    encode_acked(region_id(0), region_id(reg_id.get() + 1), 0, abacking);
    leveldb::Slice upper_bound(abacking, ACKED_BUF_SIZE);

    while (it->Valid() &&
           it->key().compare(upper_bound) < 0)
    {
        region_id tmp_ri;
        region_id tmp_reg_id;
        uint64_t tmp_seq_id;
        datalayer::returncode rc = decode_acked(e::slice(it->key().data(), it->key().size()),
                                                &tmp_ri, &tmp_reg_id, &tmp_seq_id);
        tmp_seq_id = UINT64_MAX - tmp_seq_id;

        if (rc == SUCCESS &&
            tmp_reg_id == reg_id &&
            tmp_seq_id < seq_id)
        {
            leveldb::WriteOptions wopts;
            wopts.sync = false;
            leveldb::Status st = m_db->Delete(wopts, it->key());

            if (st.ok() || st.IsNotFound())
            {
                // WOOT!
            }
            else if (st.IsCorruption())
            {
                LOG(ERROR) << "corruption at the disk layer: could not delete "
                           << reg_id << " " << seq_id << ": desc=" << st.ToString();
            }
            else if (st.IsIOError())
            {
                LOG(ERROR) << "IO error at the disk layer: could not delete "
                           << reg_id << " " << seq_id << ": desc=" << st.ToString();
            }
            else
            {
                LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
            }
        }

        it->Next();
    }
}

void
datalayer :: request_wipe(const capture_id& cid)
{
    po6::threads::mutex::hold hold(&m_block_cleaner);
    m_state_transfer_captures.insert(cid);
    m_wakeup_cleaner.broadcast();
}

void
datalayer :: cleaner()
{
    LOG(INFO) << "cleanup thread started";
    sigset_t ss;

    if (sigfillset(&ss) < 0)
    {
        PLOG(ERROR) << "sigfillset";
        return;
    }

    if (pthread_sigmask(SIG_BLOCK, &ss, NULL) < 0)
    {
        PLOG(ERROR) << "could not block signals";
        return;
    }

    while (true)
    {
        std::set<capture_id> state_transfer_captures;

        {
            po6::threads::mutex::hold hold(&m_block_cleaner);

            while ((!m_need_cleaning &&
                    m_state_transfer_captures.empty() &&
                    !m_shutdown) || m_need_pause)
            {
                m_paused = true;

                if (m_need_pause)
                {
                    m_wakeup_reconfigurer.signal();
                }

                m_wakeup_cleaner.wait();
                m_paused = false;
            }

            if (m_shutdown)
            {
                break;
            }

            m_state_transfer_captures.swap(state_transfer_captures);
            m_need_cleaning = false;
        }

        leveldb::ReadOptions opts;
        opts.fill_cache = true;
        opts.verify_checksums = true;
        std::auto_ptr<leveldb::Iterator> it;
        it.reset(m_db->NewIterator(opts));
        it->Seek(leveldb::Slice("t", 1));
        capture_id cached_cid;

        while (it->Valid())
        {
            uint8_t prefix;
            uint64_t cid;
            uint64_t seq_no;
            e::unpacker up(it->key().data(), it->key().size());
            up = up >> prefix >> cid >> seq_no;

            if (up.error() || prefix != 't')
            {
                break;
            }

            if (cid == cached_cid.get())
            {
                leveldb::WriteOptions wopts;
                wopts.sync = false;
                leveldb::Status st = m_db->Delete(wopts, it->key());

                if (st.ok() || st.IsNotFound())
                {
                    // pass
                }
                else if (st.IsCorruption())
                {
                    LOG(ERROR) << "corruption at the disk layer: could not cleanup old transfers:"
                               << " desc=" << st.ToString();
                }
                else if (st.IsIOError())
                {
                    LOG(ERROR) << "IO error at the disk layer: could not cleanup old transfers:"
                               << " desc=" << st.ToString();
                }
                else
                {
                    LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
                }

                it->Next();
                continue;
            }

            m_daemon->m_stm.report_wiped(cached_cid);

            if (!m_daemon->m_config.is_captured_region(capture_id(cid)))
            {
                cached_cid = capture_id(cid);
                continue;
            }

            if (state_transfer_captures.find(capture_id(cid)) != state_transfer_captures.end())
            {
                cached_cid = capture_id(cid);
                state_transfer_captures.erase(cached_cid);
                continue;
            }

            char tbacking[TRANSFER_BUF_SIZE];
            leveldb::Slice slice(tbacking, TRANSFER_BUF_SIZE);
            encode_transfer(capture_id(cid + 1), 0, tbacking);
            it->Seek(slice);
        }

        while (!state_transfer_captures.empty())
        {
            m_daemon->m_stm.report_wiped(*state_transfer_captures.begin());
            state_transfer_captures.erase(state_transfer_captures.begin());
        }
    }

    LOG(INFO) << "cleanup thread shutting down";
}

void
datalayer :: shutdown()
{
    bool is_shutdown;

    {
        po6::threads::mutex::hold hold(&m_block_cleaner);
        m_wakeup_cleaner.broadcast();
        is_shutdown = m_shutdown;
        m_shutdown = true;
    }

    if (!is_shutdown)
    {
        m_cleaner.join();
    }
}

datalayer :: reference :: reference()
    : m_backing()
{
}

datalayer :: reference :: ~reference() throw ()
{
}

void
datalayer :: reference :: swap(reference* ref)
{
    m_backing.swap(ref->m_backing);
}

std::ostream&
hyperdex :: operator << (std::ostream& lhs, datalayer::returncode rhs)
{
    switch (rhs)
    {
        STRINGIFY(datalayer::SUCCESS);
        STRINGIFY(datalayer::NOT_FOUND);
        STRINGIFY(datalayer::BAD_SEARCH);
        STRINGIFY(datalayer::BAD_ENCODING);
        STRINGIFY(datalayer::CORRUPTION);
        STRINGIFY(datalayer::IO_ERROR);
        STRINGIFY(datalayer::LEVELDB_ERROR);
        default:
            lhs << "unknown returncode";
    }

    return lhs;
}

datalayer :: region_iterator :: region_iterator()
    : m_dl()
    , m_snap()
    , m_iter()
    , m_region()
{
}

datalayer :: region_iterator :: ~region_iterator() throw ()
{
}

bool
datalayer :: region_iterator :: valid()
{
    if (!m_iter->Valid())
    {
        return false;
    }

    leveldb::Slice k = m_iter->key();
    uint8_t b;
    uint64_t ri;
    e::unpacker up(k.data(), k.size());
    up = up >> b >> ri;
    return !up.error() && b == 'o' && ri == m_region.get();
}

void
datalayer :: region_iterator :: next()
{
    m_iter->Next();
}

void
datalayer :: region_iterator :: unpack(e::slice* k,
                                       std::vector<e::slice>* val,
                                       uint64_t* ver,
                                       reference* ref)
{
    region_id ri;
    // XXX returncode
    decode_key(e::slice(m_iter->key().data(), m_iter->key().size()), &ri, k);
    decode_value(e::slice(m_iter->value().data(), m_iter->value().size()), val, ver);
    size_t sz = k->size();

    for (size_t i = 0; i < val->size(); ++i)
    {
        sz += (*val)[i].size();
    }

    std::vector<char> tmp(sz + 1);
    char* ptr = &tmp.front();
    memmove(ptr, k->data(), k->size());
    ptr += k->size();

    for (size_t i = 0; i < val->size(); ++i)
    {
        memmove(ptr, (*val)[i].data(), (*val)[i].size());
        ptr += (*val)[i].size();
    }

    ref->m_backing = std::string(tmp.begin(), tmp.end());
    const char* cptr = ref->m_backing.data();
    *k = e::slice(cptr, k->size());
    cptr += k->size();

    for (size_t i = 0; i < val->size(); ++i)
    {
        (*val)[i] = e::slice(cptr, (*val)[i].size());
        cptr += (*val)[i].size();
    }
}

e::slice
datalayer :: region_iterator :: key()
{
    return e::slice(m_iter->key().data(), m_iter->key().size());
}

datalayer :: snapshot :: snapshot()
    : m_dl()
    , m_snap()
    , m_checks()
    , m_ri()
    , m_backing()
    , m_range()
    , m_parse()
    , m_iter()
    , m_error(SUCCESS)
    , m_version()
    , m_key()
    , m_value()
    , m_ostr()
    , m_num_gets(0)
    , m_ref()
{
}

datalayer :: snapshot :: ~snapshot() throw ()
{
}

bool
datalayer :: snapshot :: valid()
{
    if (m_error != SUCCESS || !m_iter.get() || !m_parse)
    {
        return false;
    }

    // Don't try to optimize by replacing m_ri with a const schema* because it
    // won't persist across reconfigurations
    const schema* sc = m_dl->m_daemon->m_config.get_schema(m_ri);
    assert(sc);

    // while the most selective iterator is valid and not past the end
    while (m_iter->Valid())
    {
        if (m_iter->key().compare(m_range.limit) >= 0)
        {
            if (m_ostr) *m_ostr << " iterator retrieved " << m_num_gets << " objects from disk\n";
            return false;
        }

        (*m_parse)(m_iter->key(), &m_key);
        leveldb::ReadOptions opts;
        opts.fill_cache = true;
        opts.verify_checksums = true;
        std::vector<char> kbacking;
        leveldb::Slice lkey;
        encode_key(m_ri, m_key, &kbacking, &lkey);

        leveldb::Status st = m_dl->m_db->Get(opts, lkey, &m_ref.m_backing);

        if (st.ok())
        {
            e::slice v(m_ref.m_backing.data(), m_ref.m_backing.size());
            datalayer::returncode rc = decode_value(v, &m_value, &m_version);

            if (rc != SUCCESS)
            {
                m_error = rc;
                return false;
            }

            ++m_num_gets;
        }
        else if (st.IsNotFound())
        {
            LOG(ERROR) << "snapshot points to items (" << m_key.hex() << ") not found in the snapshot";
            m_error = CORRUPTION;
            return false;
        }
        else if (st.IsCorruption())
        {
            LOG(ERROR) << "corruption at the disk layer: region=" << m_ri
                       << " key=0x" << m_key.hex() << " desc=" << st.ToString();
            m_error = CORRUPTION;
            return false;
        }
        else if (st.IsIOError())
        {
            LOG(ERROR) << "IO error at the disk layer: region=" << m_ri
                       << " key=0x" << m_key.hex() << " desc=" << st.ToString();
            m_error = IO_ERROR;
            return false;
        }
        else
        {
            LOG(ERROR) << "LevelDB returned an unknown error that we don't know how to handle";
            m_error = LEVELDB_ERROR;
            return false;
        }

        bool passes_checks = true;

        for (size_t i = 0; passes_checks && i < m_checks->size(); ++i)
        {
            if ((*m_checks)[i].attr >= sc->attrs_sz)
            {
                passes_checks = false;
            }
            else if ((*m_checks)[i].attr == 0)
            {
                microerror e;
                passes_checks = passes_attribute_check(sc->attrs[0].type, (*m_checks)[i], m_key, &e);
            }
            else
            {
                hyperdatatype type = sc->attrs[(*m_checks)[i].attr].type;
                microerror e;
                passes_checks = passes_attribute_check(type, (*m_checks)[i], m_value[(*m_checks)[i].attr - 1], &e);
            }
        }

        if (passes_checks)
        {
            return true;
        }
        else
        {
            m_iter->Next();
        }
    }

    return false;
}

void
datalayer :: snapshot :: next()
{
    assert(m_error == SUCCESS);
    m_iter->Next();
}

void
datalayer :: snapshot :: unpack(e::slice* key, std::vector<e::slice>* val, uint64_t* ver)
{
    *key = m_key;
    *val = m_value;
    *ver = m_version;
}

void
datalayer :: snapshot :: unpack(e::slice* key, std::vector<e::slice>* val, uint64_t* ver, reference* ref)
{
    ref->m_backing = std::string();
    ref->m_backing += std::string(reinterpret_cast<const char*>(m_key.data()), m_key.size());

    for (size_t i = 0; i < m_value.size(); ++i)
    {
        ref->m_backing += std::string(reinterpret_cast<const char*>(m_value[i].data()), m_value[i].size());
    }

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(ref->m_backing.data());
    *key = e::slice(ptr, m_key.size());
    ptr += m_key.size();
    val->resize(m_value.size());

    for (size_t i = 0; i < m_value.size(); ++i)
    {
        (*val)[i] = e::slice(ptr, m_value[i].size());
        ptr += m_value[i].size();
    }

    *ver = m_version;
}
