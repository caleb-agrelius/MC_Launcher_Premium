#pragma once

// An owned POSIX file descriptor or socket, closed when it goes out of scope.
// Used by Util/Process.cpp and Platform/Platform_posix.cpp; not for Windows.
#ifndef _WIN32

#include <unistd.h>

class PosixFd {
public:
	PosixFd() = default;
	explicit PosixFd(int fd) : m_fd(fd) {}
	~PosixFd() { Reset(); }
	PosixFd(PosixFd&& other) noexcept : m_fd(other.Release()) {}
	PosixFd& operator=(PosixFd&& other) noexcept
	{
		if (this != &other)
			Reset(other.Release());
		return *this;
	}
	PosixFd(const PosixFd&) = delete;
	PosixFd& operator=(const PosixFd&) = delete;

	int Get() const { return m_fd; }
	bool Valid() const { return m_fd >= 0; }
	int Release()
	{
		const int fd = m_fd;
		m_fd = -1;
		return fd;
	}
	void Reset(int fd = -1)
	{
		if (m_fd >= 0)
			::close(m_fd);
		m_fd = fd;
	}

private:
	int m_fd = -1;
};

#endif
