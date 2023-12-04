#pragma once
#include <Windows.h>
#include <string>
class SE_Exception : public std::exception{
private:
	const unsigned int nSE;
public:
	SE_Exception() noexcept : SE_Exception{ 0 } {}
	SE_Exception(unsigned int n) noexcept : nSE{ n } {}
	unsigned int getSeNumber() const noexcept { return nSE; }
};

class AccessViolationException : public std::exception {

};

inline void ExceptionTranslator(unsigned int u, EXCEPTION_POINTERS *pExp) {
	switch (u) {
		case EXCEPTION_ACCESS_VIOLATION: 
			throw AccessViolationException();
			break;
		default: 
			throw SE_Exception(u);
			break;
		
	};
}
