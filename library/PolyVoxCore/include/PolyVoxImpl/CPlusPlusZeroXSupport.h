#ifndef __PolyVox_CPlusPlusZeroXSupport_H__
#define __PolyVox_CPlusPlusZeroXSupport_H__

//If our version of the standard library suports shared pointers then we can use that implementation, otherwise
//we use the one from boost. Slightly ugly defines here - unfortunatly C++ does not support 'typedef templates'.
//Ironically 'typedef templates' are coming in the next version of C++, by which time we won't need them.
#ifdef C_PLUS_PLUS_ZERO_X_SUPPORTED
	#include <cstdint>
	#include <memory> //Just a guess at what the standard name will be. may need changing

	#define POLYVOX_STD_NAMESPACE std
	#define POLYVOX_SHARED_PTR std::shared_ptr
	#define POLYVOX_WEAK_PTR std::weak_ptr
#else
	#include "boost/cstdint.hpp"
	#include "boost/shared_ptr.hpp"
	#include "boost/weak_ptr.hpp"

	#define POLYVOX_STD_NAMESPACE boost
	#define POLYVOX_SHARED_PTR boost::shared_ptr
	#define POLYVOX_WEAK_PTR boost::weak_ptr
#endif

//In some ways these integer types are an implementation detail and should be in the PolyVoxImpl namespace.
//However, we will be passing them into and out of PolyVox functions which are seen by the user, and we'd
//rather not have to use the PolyVoxImpl qualifier, espessially as it will show up in Doxygen. This is just a
//temporary work around until it's properly supported by C++ anyway...
namespace PolyVox
{
	typedef POLYVOX_STD_NAMESPACE::int8_t int8_t;
	typedef POLYVOX_STD_NAMESPACE::int16_t int16_t;
	typedef POLYVOX_STD_NAMESPACE::int32_t int32_t;
	typedef POLYVOX_STD_NAMESPACE::uint8_t uint8_t;
	typedef POLYVOX_STD_NAMESPACE::uint16_t uint16_t;
	typedef POLYVOX_STD_NAMESPACE::uint32_t uint32_t;
}

#endif