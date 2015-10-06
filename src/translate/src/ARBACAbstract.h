#ifndef _ARBACAbstract_H
#define _ARBACAbstract_H

#include <string.h>
#include <stdlib.h>
#include "ARBACData.h"
#include "ARBACUtil.h"
#include "ARBACTransform.h"

/*****************************************************************
 *  Define the data structure for tranformation                  *
 *****************************************************************/
// Data type for a Venn region
typedef struct _venn_region
{
	set P; // P set
	set N; // N set
} venn_region;

// Define the Track set
venn_region* Track;
int Track_size;

char * query;

// The Venn region string array generated from Track
char** venn_region_array;
int venn_region_array_size;

// User configuration set
set * user_config_array;
int user_config_array_size;

void
build_user_configurations();

int
get_number_of_venn_region(venn_region);

void
build_Track();

void
build_Venn_region_string_array();

void
free_abstract_temp_data();
#endif
