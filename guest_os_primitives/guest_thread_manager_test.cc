/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtest/gtest.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <thread>

#include "berberis/base/gettid.h"
#include "berberis/guest_os_primitives/guest_thread.h"
#include "berberis/guest_os_primitives/guest_thread_manager.h"

#include "guest_thread_manager_impl.h"
#include "guest_thread_map.h"

namespace berberis {

namespace {

// Returns the pid of a child process that has exited and been reaped, or -1.
pid_t GetPidOfGoneProcess() {
  pid_t pid = fork();
  if (pid == 0) {
    _exit(0);
  }
  if (pid < 0 || waitpid(pid, nullptr, 0) != pid) {
    return -1;
  }
  return pid;
}

// Creates the GuestThread of a thread of a child process that ran in our memory (clone with
// CLONE_VM and without CLONE_THREAD) and left through exit_group or execve, so that its entry is
// still in the table.
GuestThread* CreateThreadOfGoneProcess() {
  pid_t pid = GetPidOfGoneProcess();
  if (pid < 0) {
    return nullptr;
  }
  GuestThread* thread = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  if (thread != nullptr) {
    thread->SetProcessId(pid);
  }
  return thread;
}

// One of our threads gets the tid of a thread of a gone process. Attaching it must not pick up the
// dead thread's GuestThread. The tid is not the gone process's pid, as for any thread of it but the
// first.
TEST(GuestThreadManager, AttachReplacesThreadOfGoneProcess) {
  GuestThread* gone = CreateThreadOfGoneProcess();
  ASSERT_NE(gone, nullptr);
  std::thread([gone] {
    pid_t tid = GettidSyscall();
    GuestThreadMap::GetInstance()->InsertThread(tid, gone);
    GuestThread* thread = GetCurrentGuestThread();
    // The dead thread's GuestThread is unmapped and the new one may get its address, so tell them
    // apart by process.
    EXPECT_EQ(thread->GetProcessId(), GetpidSyscall());
    EXPECT_EQ(GuestThreadMap::GetInstance()->FindThread(tid), thread);
  }).join();
}

// Same, for a thread that inserts its own GuestThread when it starts.
TEST(GuestThreadManager, InsertReplacesThreadOfGoneProcess) {
  GuestThread* gone = CreateThreadOfGoneProcess();
  ASSERT_NE(gone, nullptr);
  GuestThread* thread = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  ASSERT_NE(thread, nullptr);
  std::thread([gone, thread] {
    pid_t tid = GettidSyscall();
    GuestThreadMap::GetInstance()->InsertThread(tid, gone);
    InsertCurrentThread(thread, false);
    EXPECT_EQ(GuestThreadMap::GetInstance()->FindThread(tid), thread);
    EXPECT_EQ(thread->GetProcessId(), GetpidSyscall());
    DetachCurrentThread();
  }).join();
}

// A thread of the caller's own process keeps its GuestThread, also when its tid is not the pid.
TEST(GuestThreadManager, AttachKeepsThreadOfOwnProcess) {
  GuestThread* own = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  ASSERT_NE(own, nullptr);
  own->SetProcessId(GetpidSyscall());
  std::thread([own] {
    GuestThreadMap::GetInstance()->InsertThread(GettidSyscall(), own);
    EXPECT_EQ(GetCurrentGuestThread(), own);
    DetachCurrentThread();
  }).join();
}

// After fork the child is another process, and its thread keeps its GuestThread.
TEST(GuestThreadManager, ForkedChildKeepsItsThread) {
  GuestThread* thread = GetCurrentGuestThread();
  pid_t pid = fork();
  if (pid == 0) {
    ResetCurrentGuestThreadAfterFork(thread);
    _exit(GetCurrentGuestThread() == thread && thread->GetProcessId() == GetpidSyscall() ? 0 : 1);
  }
  ASSERT_GT(pid, 0);
  int status;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

// Of several threads that try to release the same entry, only one gets it.
TEST(GuestThreadMap, RemoveThreadIf) {
  pid_t tid = GetPidOfGoneProcess();
  ASSERT_GT(tid, 0);
  GuestThread* thread = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  ASSERT_NE(thread, nullptr);
  GuestThreadMap* map = GuestThreadMap::GetInstance();
  map->InsertThread(tid, thread);
  EXPECT_EQ(map->RemoveThreadIf(tid, [](GuestThread*) { return false; }), nullptr);
  EXPECT_EQ(map->FindThread(tid), thread);
  EXPECT_EQ(map->RemoveThreadIf(tid, [](GuestThread*) { return true; }), thread);
  EXPECT_EQ(map->FindThread(tid), nullptr);
  EXPECT_EQ(map->RemoveThreadIf(tid, [](GuestThread*) { return true; }), nullptr);
  GuestThread::Destroy(thread);
}

// A GuestThread created after another one was destroyed has another serial, also if it got the
// same address.
TEST(GuestThread, SerialIsNotReused) {
  GuestThread* first = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  ASSERT_NE(first, nullptr);
  uint64_t serial = first->GetSerial();
  GuestThread::Destroy(first);
  GuestThread* second = GuestThread::CreateClone(GetCurrentGuestThread(), false);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second->GetSerial(), serial);
  GuestThread::Destroy(second);
}

}  // namespace

}  // namespace berberis
