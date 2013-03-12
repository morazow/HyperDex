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

#ifndef hyperdex_client_description_h_
#define hyperdex_client_description_h_

// STL
#include <vector>
#include <utility>

// HyperDex
#include "common/ids.h"
#include "client/hyperclient.h"

class hyperclient::description
{
    public:
        description(const char** desc);
        ~description() throw ();

    public:
        void compile();
        bool last_reference();
        void add_text(const hyperdex::virtual_server_id& vid, const e::slice& text);
        void add_text(const hyperdex::virtual_server_id& vid, const char* text);

    private:
        friend class e::intrusive_ptr<description>;
        void inc() { ++m_ref; }
        void dec() { if (--m_ref == 0) delete this; }

    private:
        description(const description&);
        description& operator = (const description&);

    private:
        uint64_t m_ref;
        const char** m_desc;
        std::vector<std::pair<hyperdex::virtual_server_id, std::string> > m_msgs;
};

#endif // hyperdex_client_description_h_
