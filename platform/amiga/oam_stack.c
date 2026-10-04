#include "oam_stack.h"

#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

/* statics: once the stack is swapped, nothing of the old frame is used */
static struct StackSwapStruct swap;
static int (*run_fn)(int, char **);
static int run_argc, run_rc;
static char **run_argv;

/* The call with arguments has a function of its own, so they are pushed and
 * popped on the new stack. Made between the two StackSwap calls, GCC may pop
 * them only after the swap back, off the old stack, when it omits the frame
 * pointer; the return then goes astray. */
static void __attribute__((noinline)) call_fn(void)
{
    run_rc = run_fn(run_argc, run_argv);
}

static void __attribute__((noinline)) run_on_new_stack(void)
{
#ifdef __AROS__
    struct StackSwapArgs none = { { 0 } };      /* AROS: StackSwap is deprecated */
    NewStackSwap(&swap, (APTR)call_fn, &none);
#else
    StackSwap(&swap);
    call_fn();
    StackSwap(&swap);
#endif
}

int oam_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    APTR mem;
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= bytes) return fn(argc, argv);
    mem = AllocVec(bytes, MEMF_ANY);
    if (!mem) return fn(argc, argv);            /* run anyway, on what there is */
    swap.stk_Lower = mem;
#ifdef __AROS__
    swap.stk_Upper = (APTR)((UBYTE *)mem + bytes);
#else
    swap.stk_Upper = (ULONG)mem + bytes;
#endif
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    run_fn = fn;
    run_argc = argc;
    run_argv = argv;
    run_on_new_stack();
    FreeVec(mem);
    return run_rc;
}
