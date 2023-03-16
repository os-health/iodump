/*
 *
 *  Copyright (c) 2022, Alibaba Group;
 *  Licensed under the MIT License (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *       https://mit-license.org/
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

int string_match(char *pattern, char *bematch);
void sign_handler(int sig, siginfo_t *siginfo, void *context);
void print_usage(FILE *stream);

struct output_option{
    unsigned long bit;
    char         *option;
};

struct blocktrace{
    int   type;
    char* tracename;
};
