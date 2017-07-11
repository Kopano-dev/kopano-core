#include <type_traits>
#include <cstring>
#include "soapH.h"
#include "soapStub.h"
#define C(X) X::X(void) { static_assert(std::is_standard_layout<X>::value, ""); memset(this, 0, sizeof(*this)); }
C(_act)
C(propTagArray)
C(propValData)

mv_binary::mv_binary() : __ptr(), __size() {}
mv_double::mv_double() : __ptr(), __size() {}
mv_hiloLong::mv_hiloLong() : __ptr(), __size() {}
mv_i2::mv_i2() : __ptr(), __size() {}
mv_i8::mv_i8() : __ptr(), __size() {}
mv_long::mv_long() : __ptr(), __size() {}
mv_r4::mv_r4() : __ptr(), __size() {}
mv_string8::mv_string8() : __ptr(), __size() {}

propTagArray::propTagArray(unsigned int *a, int b) :
	__ptr(a), __size(b)
{}

xsd__base64Binary::xsd__base64Binary() : __ptr(), __size() {}
