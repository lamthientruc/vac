#ifndef COUNTEREXAMPLE_H_
#define COUNTEREXAMPLE_H_

#include "CounterExampleData.h"

int
hasCounterExample;

void
readCBMCXMLLog(char *);

void
readCBMCTranslated(char *);

void
readARBAC(char *);

void
readSimplifyLog(char *);

void
reduction_finiteARBAC(void);

void
generate_counter_example(char *, char *, char *, FILE *);

void
generate_counter_example_full(char *, char *, char *, char *, char *, FILE *);

#endif /* COUNTEREXAMPLE_H_ */
