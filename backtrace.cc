/*
 * Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "v8.h"
#include "node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <cxxabi.h>
#include <dlfcn.h>

extern "C" {
void jsbacktrace(void);
int main(int, char**);  // Top of the stack.
}

namespace
{

#define OFFSET(base, addr)                                                    \
  (static_cast<long>(static_cast<const char*>(addr) -                         \
                     static_cast<const char*>(base)))

// Assumes -fno-omit-frame-pointer.
struct Frame
{
  const Frame* frame_pointer;
  const void* return_address;
};

// Linked list. Wildly inefficient.
struct Code
{
  Code* next;
  const void* start;
  const void* end;
  char name[1];  // Variadic length.
};

void sigabrt(int);
void find_stack_top(const Frame* frame);
void walk_stack_frames(unsigned skip, void (*cb)(const Frame* frame));
v8::Handle<v8::Value> backtrace(const v8::Arguments&);
void print_stack_frame(const Frame* frame);
bool print_c_frame(const Frame* frame, FILE* stream);
bool print_js_frame(const Frame* frame, FILE* stream);
Code* find_code(const void* addr);
void add_code(const char* name,
              unsigned int namelen,
              const void* start,
              const void* end);
void free_code();
void jit_code_event(const v8::JitCodeEvent* ev);

struct Code* code_head;
int stack_trace_index;
v8::Local<v8::StackTrace> stack_trace;

const Frame* stack_top = reinterpret_cast<const Frame*>(-1);

void init(v8::Handle<v8::Object> module)
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags = SA_RESETHAND;
  sa.sa_handler = sigabrt;
  sigaction(SIGABRT, &sa, NULL);
  walk_stack_frames(0, find_stack_top);
  module->Set(v8::String::New("backtrace"),
              v8::FunctionTemplate::New(backtrace)->GetFunction());
}

void find_stack_top(const Frame* frame)
{
  Dl_info info;
  if (dladdr(frame->return_address, &info) == 0)
    return;
  if (strcmp(info.dli_sname, "start") != 0)
    return;
  stack_top = frame->frame_pointer;
}

v8::Handle<v8::Value> backtrace(const v8::Arguments&)
{
  jsbacktrace();
  return v8::Undefined();
}

void sigabrt(int)
{
  jsbacktrace();
  raise(SIGABRT);
}

// Externally visible.
extern "C" void jsbacktrace(void)
{
  walk_stack_frames(1, print_stack_frame);
  free_code();
}

void print_stack_frame(const Frame* frame)
{
  FILE* stream = stderr;

  if (print_c_frame(frame, stream))
    return;

  if (print_js_frame(frame, stream))
    return;

  // Unresolved. Just print the raw address.
  fprintf(stream, "%lx\n", reinterpret_cast<long>(frame->return_address));
}

bool print_c_frame(const Frame* frame, FILE* stream)
{
  Dl_info info;

  if (dladdr(frame->return_address, &info) == 0)
    return false;

  const char* name = info.dli_sname;
  const char* demangled_name = abi::__cxa_demangle(name, NULL, NULL, NULL);

  if (demangled_name != NULL)
    name = demangled_name;

  fprintf(stream,
          "%lx+%lx\t%s %s(%p)\n",
          reinterpret_cast<long>(info.dli_saddr),
          OFFSET(info.dli_saddr, frame->return_address),
          name,
          info.dli_fname,
          info.dli_fbase);

  if (name == demangled_name)
    free(const_cast<char*>(name));

  return true;
}

bool print_js_frame(const Frame* frame, FILE* stream)
{
  if (code_head == NULL) {
    // Lazy init.
    v8::V8::SetJitCodeEventHandler(v8::kJitCodeEventEnumExisting,
                                   jit_code_event);
    v8::V8::SetJitCodeEventHandler(v8::kJitCodeEventDefault, NULL);
    stack_trace = v8::StackTrace::CurrentStackTrace(64);
    stack_trace_index = 0;
  }

  Code* code = find_code(frame->return_address);
  if (code == NULL)
    return false;

  if (stack_trace_index < stack_trace->GetFrameCount()) {
    v8::Local<v8::StackFrame> js_frame =
        stack_trace->GetFrame(stack_trace_index++);
    v8::String::Utf8Value function_name(js_frame->GetFunctionName());
    if (function_name.length() > 0) {
      v8::String::Utf8Value script_name(js_frame->GetScriptName());
      fprintf(stream,
              "%lx+%lx\t%s %s:%d:%d\n",
              reinterpret_cast<long>(code->start),
              OFFSET(code->start, frame->return_address),
              *function_name,
              *script_name,
              js_frame->GetLineNumber(),
              js_frame->GetColumn());
      return true;
    }
  }

  fprintf(stream,
          "%lx+%lx\t%s\n",
          reinterpret_cast<long>(code->start),
          OFFSET(code->start, frame->return_address),
          code->name);
  return true;
}

Code* find_code(const void* addr)
{
  for (Code* code = code_head; code != NULL; code = code->next)
    if (code->start <= addr && code->end >= addr)
      return code;
  return NULL;
}

void add_code(const char* name,
              unsigned int namelen,
              const void* start,
              const void* end)
{
  Code* code = static_cast<Code*>(malloc(sizeof(*code) + namelen));
  if (code == NULL) return;
  memcpy(code->name, name, namelen);
  code->name[namelen] = '\0';
  code->start = start;
  code->end = end;
  code->next = code_head;
  code_head = code;
}

void free_code()
{
  while (code_head != NULL) {
    Code* code = code_head;
    code_head = code->next;
    free(code);
  }
}

void jit_code_event(const v8::JitCodeEvent* ev)
{
  if (ev->type == v8::JitCodeEvent::CODE_ADDED) {
    add_code(ev->name.str,
             ev->name.len,
             ev->code_start,
             static_cast<const char*>(ev->code_start) + ev->code_len);
  }
}

__attribute__((noinline))
void walk_stack_frames(unsigned skip, void (*cb)(const Frame* frame))
{
  const Frame* frame;
  __asm__ __volatile__ ("mov %%rbp, %0" : "=g" (frame));
  do
    if (skip == 0)
      cb(frame);
    else
      skip -= 1;
  while ((frame = frame->frame_pointer) < stack_top);
}

}  // anonymous namespace

NODE_MODULE(backtrace, init)
