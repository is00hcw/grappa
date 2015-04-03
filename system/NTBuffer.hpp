#pragma once
////////////////////////////////////////////////////////////////////////
// This file is part of Grappa, a system for scaling irregular
// applications on commodity clusters. 

// Copyright (C) 2010-2014 University of Washington and Battelle
// Memorial Institute. University of Washington authorizes use of this
// Grappa software.

// Grappa is free software: you can redistribute it and/or modify it
// under the terms of the Affero General Public License as published
// by Affero, Inc., either version 1 of the License, or (at your
// option) any later version.

// Grappa is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// Affero General Public License for more details.

// You should have received a copy of the Affero General Public
// License along with this program. If not, you may obtain one from
// http://www.affero.org/oagpl.html.
////////////////////////////////////////////////////////////////////////

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <x86intrin.h>

namespace Grappa {
namespace impl {

class NTBuffer {
  static const int local_buffer_size = 8;
  static const int last_position = 7;

  uint64_t localbuf_[ local_buffer_size ];

  // TODO: squeeze these all into one 64-bit word (the last word of the buffer)
  //
  // Buffer_ must be 64-byte aligned, so we only need 42 bits max of
  // address.
  //
  // Position_ and local_position_ count 8-byte words, and can be
  // combined (masking off the lower three bits for the local part).
  //
  // (or if 64 bits is not enough, messages could be multiples of 16 bytes instead of 8)
  uint64_t * buffer_;
  int position_;
  int local_position_;

  void get_new_buffer( ) {
  }
  
public:
  NTBuffer()
    : buffer_( nullptr )
    , position_( -1 )
    , local_position_( 0 )
  { }

  void * get_buffer() const {
    _mm_sfence();
    return reinterpret_cast<void*>(buffer_);
  }
  
  void flush() {
    if( local_position_ > 0 ) { // skip unless we have something to write
      for( int i = local_position_; i < local_buffer_size; ++i ) {
        localbuf_[i] = 0;
      }
      
      if( !buffer_ ) {
        // get buffer
        //buffer_ = new __attribute__((aligned(64))) uint64_t[1024];
        posix_memalign( reinterpret_cast<void**>(&buffer_), 64, 1 << 20);
        position_ = 0;
      }

      // write out full cacheline
      __m128i * src = reinterpret_cast<__m128i*>( &localbuf_[0] );
      __m128i * dst = reinterpret_cast<__m128i*>( &buffer_[position_] );
      _mm_stream_si128( dst+0, *(src+0) );
      _mm_stream_si128( dst+1, *(src+1) );
      _mm_stream_si128( dst+2, *(src+2) );
      _mm_stream_si128( dst+3, *(src+3) );

      position_ += local_buffer_size; // advance to next cacheline of output buffer
      local_position_ = 0;
    }
  }
  
  inline void enqueue( uint64_t * word_p, int word_size ) {
    while( word_size > 0 ) {
      localbuf_[ local_position_ ] = *word_p;
      ++word_p;
      --word_size;
      ++local_position_;

      if( local_position_ == local_buffer_size ) {
        flush();
      }
    }
  }

} __attribute__((aligned(64)));

template< typename T >
inline void nt_enqueue( NTBuffer * b, T * p, int size ) {
  _mm_prefetch( b, _MM_HINT_NTA );
  _mm_prefetch( reinterpret_cast<char*>(b)+64, _MM_HINT_NTA );
  DCHECK_EQ( reinterpret_cast<uint64_t>(p) % 8, 0 ) << "Pointer must be 8-byte aligned";
  DCHECK_EQ( size % 8, 0 ) << "Size must be a multiple of 8";
  b->enqueue( reinterpret_cast<uint64_t*>(p), size/8 );
}

void nt_flush( NTBuffer * b ) {
  _mm_prefetch( b, _MM_HINT_NTA );
  _mm_prefetch( reinterpret_cast<char*>(b)+64, _MM_HINT_NTA );
  b->flush();
}

}
}