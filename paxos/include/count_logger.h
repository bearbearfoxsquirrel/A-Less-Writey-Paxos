//
// Created by Michael Davis on 02/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_COUNT_LOGGER_H
#define A_LESS_WRITEY_PAXOS_COUNT_LOGGER_H

struct count_logger;

struct count_logger *count_logger_new(const char *name, unsigned int name_len);

void count_logger_free(struct count_logger** count_logger);

void count_logger_increment(struct count_logger* count_logger, const unsigned int n);

void count_logger_decrement(struct count_logger* count_logger, const unsigned int n);

void count_logger_print(const struct count_logger *count_logger);

void count_logger_clear(struct count_logger* count_logger);

void count_logger_increment_and_print(struct count_logger* count_logger, int n);

void count_logger_decrement_and_print(struct count_logger* count_logger, int n);

void count_logger_print_and_clear(struct count_logger* count_logger);


#endif //A_LESS_WRITEY_PAXOS_COUNT_LOGGER_H
