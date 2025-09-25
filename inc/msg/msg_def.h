#ifndef __MSG_DEF_H__
#define __MSG_DEF_H__

#include <vector>

namespace commutil {

/** @typedef Defines a message buffer type. */
typedef std::vector<char> MsgBuffer;

/** @typedef Defines a message buffer array type. */
typedef std::vector<MsgBuffer> MsgBufferArray;

}  // namespace commutil

#endif