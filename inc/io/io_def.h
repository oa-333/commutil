#ifndef __IO_DEF_H__
#define __IO_DEF_H__

#include <cstdint>

namespace commutil {

enum class ByteOrder : uint32_t { HOST_ORDER, NETWORK_ORDER };

}

#endif  // __IO_DEF_H__