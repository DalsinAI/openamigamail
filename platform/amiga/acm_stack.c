#include "acm_stack.h"

#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

/* statics: once the stack is swapped, nothing of the old frame is used */
static struct StackSwapStruct swap;
static int (*run_fn)(int, char **);
static int run_argc, run_rc;
static char **run_argv;

static void __attribute__((noinline)) run_on_new_stack(void)
{
    StackSwap(&swap);
    run_rc = run_fn(run_argc, run_argv);
    StackSwap(&swap);
}

int acm_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    APTR mem;
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= bytes) return fn(argc, argv);
    mem = AllocVec(bytes, MEMF_ANY);
    if (!mem) return fn(argc, argv);            /* run anyway, on what there is */
    swap.stk_Lower = mem;
    swap.stk_Upper = (ULONG)mem + bytes;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    run_fn = fn;
    run_argc = argc;
    run_argv = argv;
    run_on_new_stack();
    FreeVec(mem);
    return run_rc;
}
