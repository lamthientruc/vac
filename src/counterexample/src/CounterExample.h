#ifndef COUNTEREXAMPLE_H_
#define COUNTEREXAMPLE_H_

#include "CounterExampleData.h"

FILE * debugFile;

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
generate_counter_example(int, char**);

void
generate_counter_example_full(int, char**);

#endif /* COUNTEREXAMPLE_H_ */
