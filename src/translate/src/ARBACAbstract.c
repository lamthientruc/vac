#include "ARBACAbstract.h"

/**********************************************************************
 * Function free_Venn_region
 * Free a Venn region data
 **********************************************************************/
static void
free_Venn_region(venn_region v)
{
    free(v.P.array);
    free(v.N.array);
}

/**********************************************************************
 * Function equal_Venn_regions
 * Check if two Venn region are equal
 * @return : true if equal 0 otherwise
 **********************************************************************/
static int
equal_Venn_regions(venn_region v1, venn_region v2)
{
    if (equal_set(v1.P, v2.P) && equal_set(v1.N, v2.N))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/**********************************************************************
 * Function belong_to_Track
 * Check if some Venn region is belong to Track set
 **********************************************************************/
static int
belong_to_Track(venn_region v)
{
    int i;
    for (i = 0; i < Track_size; i++)
    {
        if (equal_Venn_regions(v, Track[i]))
            return 1;
    }
    return 0;
}

/**********************************************************************
 * Function add_Venn_region_to_track
 * Add a Venn region to Track set
 **********************************************************************/
static void
add_Venn_region_to_Track(venn_region v)
{
    if (!belong_to_Track(v))
    {
        Track_size++;
        Track = realloc(Track, Track_size * sizeof(venn_region));
        Track[Track_size - 1].P = duplicate_set(v.P);
        Track[Track_size - 1].N = duplicate_set(v.N);
    }
}

// Build user configuration array, the set of roles that assigned to each user
void
build_user_configurations()
{
    int i;

    user_config_array_size = user_array_size;
    user_config_array = malloc(user_config_array_size * sizeof(set));

    // Initialize
    for (i = 0; i < user_config_array_size; i++)
    {
        user_config_array[i].array = 0;
        user_config_array[i].array_size = 0;
    }

    // Build user configuration array
    for (i = 0; i < ua_array_size; i++)
    {
        user_config_array[ua_array[i].user_index] = add_last_element(user_config_array[ua_array[i].user_index], ua_array[i].role_index);
    }
}

// Get the number of users in the specific venn region
int
get_number_of_venn_region(venn_region v)
{
    int i, count = 0;

    for (i = 0; i < user_config_array_size; i++)
    {
        if (subset_of(v.P, user_config_array[i]) && intersect_set(v.N, user_config_array[i]).array_size == 0)
        {
            count++;
        }
    }
    return count;
}

// Get the number of administrator present in the system
int
get_number_of_administrator(int admin_role_index)
{
    int i, count = 0;
    for (i = 0; i < user_config_array_size; i++)
    {
        if (belong_to(user_config_array[i].array, user_config_array[i].array_size, admin_role_index))
        {
            count++;
        }
    }
    return count;
}

/**********************************************************************
 * Function build_Track
 * Build the Track set following the paper theory
 **********************************************************************/
void
build_Track()
{
    int i;
    venn_region v; /* Temporary venn_region */
    int *tmp = 0;

    v.P.array = 0;
    v.P.array_size = 0;
    v.N.array = 0;
    v.N.array_size = 0;

    // Initialize track
    Track = 0;
    Track_size = 0;

    // for each can assign rule, Track contains (Pos, Neg) and (admin, 0)
    for (i = 0; i < ca_array_size; i++)
    {
        // Add (Pos, Neg) to Track
        if (ca_array[i].type == 0)
        {
            v.P = build_set(ca_array[i].positive_role_array, ca_array[i].positive_role_array_size);
            v.N = build_set(ca_array[i].negative_role_array, ca_array[i].negative_role_array_size);

            // Add to Track
            add_Venn_region_to_Track(v);

            free(v.P.array);
            v.P.array = 0;
            free(v.N.array);
            v.N.array = 0;
        }

        // Add (admin, 0) to Track
        tmp = malloc(sizeof(int));
        tmp[0] = ca_array[i].admin_role_index;
        v.P = build_set(tmp, 1);
        v.N = build_set(0, 0);

        add_Venn_region_to_Track(v);

        // Reuse temporary tmp
        free(tmp);
        tmp = 0;

        free(v.P.array);
        v.P.array = 0;
        free(v.N.array);
        v.N.array = 0;
    }

    // for each can revoke rule, Track contains (admin, 0)
    for (i = 0; i < cr_array_size; i++)
    {
        // Add (admin, 0) to Track
        tmp = malloc(sizeof(int));
        tmp[0] = cr_array[i].admin_role_index;
        v.P = build_set(tmp, 1);
        v.N = build_set(0, 0);
        add_Venn_region_to_Track(v);
        free(tmp);
        tmp = 0;

        free(v.P.array);
        v.P.array = 0;
    }

    // Track also contains (goal, 0)
    tmp = malloc(sizeof(int));
    tmp[0] = goal_role_index;
    v.P = build_set(tmp, 1);
    v.N = build_set(0, 0);

    add_Venn_region_to_Track(v);
    free(tmp);
    free(v.P.array);
    v.P.array = 0;
}

/**********************************************************************
 * Function Venn_region2string
 * Convert a Venn region ro string as a variable C(P,N)
 **********************************************************************/
static char *
Venn_region2string(venn_region v)
{
    int i;
    char tmp_str[2000] = "";
    char re_str[2000] = "";

    strcat(tmp_str, "n");

    for (i = 0; i < v.P.array_size; i++)
    {
        strcat(tmp_str, "_");
        strcat(tmp_str, role_array[v.P.array[i]]);
    }

    for (i = 0; i < v.N.array_size; i++)
    {
        strcat(tmp_str, "_0");
        strcat(tmp_str, role_array[v.N.array[i]]);
    }

    strcpy(re_str, tmp_str);

    memset(tmp_str, 0, sizeof(tmp_str));

    return strdup(re_str);
}

/**********************************************************************
 * Function build_Venn_region_string_array
 * Build an array to store all Venn region to string
 **********************************************************************/
void
build_Venn_region_string_array()
{
    int i;
    char string[2000] = "";

    venn_region_array = 0;
    venn_region_array_size = Track_size;
    venn_region_array = malloc(Track_size * sizeof(char *));

    for (i = 0; i < Track_size; i++)
    {
        strcpy(string, Venn_region2string(Track[i]));
        venn_region_array[i] = malloc(strlen(string) + 1);
        strcpy(venn_region_array[i], string);
    }
}

// Free data used for abstract translation
void
free_abstract_temp_data()
{
    int i;
    for (i = 0; i < Track_size; i++)
    {
        free_Venn_region(Track[i]);
    }
    free(Track);

    for (i = 0; i < venn_region_array_size; i++)
    {
        free(venn_region_array[i]);
    }
    free(venn_region_array);

    for (i = 0; i < user_config_array_size; i++)
    {
        free(user_config_array[i].array);
    }
    free(user_config_array);
}
