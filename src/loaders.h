#ifndef LOADERS_H
#define LOADERS_H

#include "def.h"

int load_dictionary(FILE*, Dictionary*);
int load_quotes(FILE*, Quotes*);
int load_hist(FILE*, History*);

#endif /* LOADERS_H */
