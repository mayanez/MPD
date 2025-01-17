// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "MultiSocketMonitor.hxx"
#include "Loop.hxx"

#include <algorithm>

#ifdef USE_EPOLL
#include <cerrno>
#endif

#ifndef _WIN32
#include <poll.h>
#endif

MultiSocketMonitor::MultiSocketMonitor(EventLoop &_loop) noexcept
	:idle_event(_loop, BIND_THIS_METHOD(OnIdle)),
	 timeout_event(_loop, BIND_THIS_METHOD(OnTimeout)) {
}

void
MultiSocketMonitor::Reset() noexcept
{
	assert(GetEventLoop().IsInside());

	fds.clear();
#ifdef USE_EPOLL
	always_ready_fds.clear();
#endif
	idle_event.Cancel();
	timeout_event.Cancel();
	ready = refresh = false;
}

bool
MultiSocketMonitor::AddSocket(SocketDescriptor fd, unsigned events) noexcept
{
	fds.emplace_front(*this, fd);
	bool success = fds.front().Schedule(events);
	if (!success) {
		fds.pop_front();

#ifdef USE_EPOLL
		if (errno == EPERM)
			/* not supported by epoll (e.g. "/dev/null"):
			   add it to the "always ready" list */
			always_ready_fds.push_front({fd, events});
#endif
	}

	return success;
}

void
MultiSocketMonitor::ClearSocketList() noexcept
{
	assert(GetEventLoop().IsInside());

	fds.clear();
#ifdef USE_EPOLL
	always_ready_fds.clear();
#endif
}

#ifndef _WIN32

void
MultiSocketMonitor::ReplaceSocketList(std::span<pollfd> pfds) noexcept
{
#ifdef USE_EPOLL
	always_ready_fds.clear();
#endif

	UpdateSocketList([pfds](SocketDescriptor fd) -> unsigned {
		auto i = std::find_if(pfds.begin(), pfds.end(), [fd](const struct pollfd &pfd){
			return pfd.fd == fd.Get();
		});

		if (i == pfds.end())
			return 0;

		return std::exchange(i->events, 0);
	});

	for (const auto &i : pfds)
		if (i.events != 0)
			AddSocket(SocketDescriptor(i.fd), i.events);
}

#endif

void
MultiSocketMonitor::Prepare() noexcept
{
	auto timeout = PrepareSockets();

#ifdef USE_EPOLL
	if (!always_ready_fds.empty()) {
		/* if there was at least one file descriptor not
		   supported by epoll, install a very short timeout
		   because we assume it's always ready */
		constexpr Event::Duration ready_timeout =
			std::chrono::milliseconds(1);
		if (timeout < timeout.zero() || timeout > ready_timeout)
			timeout = ready_timeout;
	}
#endif

	if (timeout >= timeout.zero())
		timeout_event.Schedule(timeout);
	else
		timeout_event.Cancel();

}

void
MultiSocketMonitor::OnIdle() noexcept
{
	if (ready) {
		ready = false;
		DispatchSockets();

		/* TODO: don't refresh always; require users to call
		   InvalidateSockets() */
		refresh = true;
	}

	if (refresh) {
		refresh = false;
		Prepare();
	}
}
