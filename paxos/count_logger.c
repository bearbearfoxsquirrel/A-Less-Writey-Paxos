//
// Created by Michael Davis on 02/11/2020.
//
#include <count_logger.h>

#include <stdlib.h>
#include <string.h>
#include <paxos.h>


struct count_logger {
    char* counter_name;
    uint count;
};

struct count_logger *count_logger_new(const char *name, unsigned int name_len) {
    struct count_logger* ret = malloc(sizeof(*ret));
    char* name_cpy = malloc(sizeof(*name_cpy) * name_len);
    strcpy(name_cpy, name);
    name_cpy[name_len - 1]  = '\0';
    *ret = (struct count_logger){
            .counter_name = name_cpy,
            .count = 0
    };
    return ret;
}

void count_logger_free(struct count_logger** count_logger) {
    free((*count_logger)->counter_name);
    (*count_logger)->counter_name = NULL;
    free(*count_logger);
    *count_logger = NULL;

}

void count_logger_increment(struct count_logger* count_logger, const unsigned int n){
    count_logger->count += n;
}

void count_logger_print_and_clear(struct count_logger* count_logger){
    count_logger_print(count_logger);
    count_logger_clear(count_logger);
}

void count_logger_clear(struct count_logger* count_logger){
    count_logger->count = 0;
}

void count_logger_decrement(struct count_logger* count_logger, const unsigned int n){
    count_logger->count -= n;
}

void count_logger_print(const struct count_logger *count_logger){
    paxos_log_debug("%s : %u", count_logger->counter_name, count_logger->count);
}

void count_logger_increment_and_print(struct count_logger* count_logger, int n){
    count_logger_increment(count_logger, n);
    count_logger_print(count_logger);
}

void count_logger_decrement_and_print(struct count_logger* count_logger, int n){
    count_logger_decrement(count_logger, n);
    count_logger_print(count_logger);
}

