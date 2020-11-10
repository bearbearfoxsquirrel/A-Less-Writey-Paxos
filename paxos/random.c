//
// Created by Michael Davis on 23/03/2020.
//


#include <random.h>
#include <stdlib.h>


unsigned int random_between(unsigned int min, unsigned int max) {
    unsigned int r;
    const unsigned int range = 1 + max - min;
    const unsigned int buckets = RAND_MAX / range;
    const unsigned int limit = buckets * range;

    /* Create equal size buckets all in a row, then fire randomly towards
     * the buckets until you land in one of them. All buckets are equally
     * likely. If you land off the end of the line of buckets, try again. */
    do
    {
        r = rand();
    } while (r >= limit);

    return min + (r / buckets);
}
int compare( const void* a, const void* b)
{
    unsigned int int_a = * ( (unsigned int*) a );
    unsigned int int_b = * ( (unsigned int*) b );

    if ( int_a == int_b ) return 0;
    else if ( int_a < int_b ) return -1;
    else return 1;
}

unsigned int random_between_excluding(unsigned int min, unsigned int max, const unsigned int* excluding, unsigned int number_of_excluding) {
    qsort(excluding, number_of_excluding, sizeof(*excluding), compare);
    int next_range = max - min + 1 - number_of_excluding;

    unsigned int random;
    if (((int)(1 + next_range - min)) <= 0) {
        random = (rand() % max) + min; // cannot divide by zero so fall back to bad rand
    } else {
        random = random_between(min, next_range);
    }
    for (unsigned int ex = 0; ex < number_of_excluding; ex++) {
        if (random < excluding[ex]) {
            break;
        }
        random++;
    }
    return random;
}
