/* acm_stack: run a program's main on a stack of its own size. A Shell
 * gives commands 4 KB unless told otherwise, and ACMail needs more (a TLS
 * handshake alone runs deep), so main swaps to a bigger stack with exec's
 * StackSwap when the one it was given is smaller. */
#ifndef ACM_STACK_H
#define ACM_STACK_H

int acm_run_with_stack(unsigned long bytes, int (*fn)(int argc, char **argv), int argc, char **argv);

#endif
