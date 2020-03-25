//
// Created by Michael Davis on 23/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_RANDOM_H
#define A_LESS_WRITEY_PAXOS_RANDOM_H



unsigned int random_between(unsigned int min, unsigned int max);

unsigned int random_between_excluding(unsigned int min, unsigned int max, const unsigned int* excluding, unsigned int number_of_excluding);

#endif //A_LESS_WRITEY_PAXOS_RANDOM_H
