#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

// Function to print usage information
void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -n, --iterations NUM    Number of D-Bus calls to make (default: 1)\n");
    printf("  -b, --bytes NUM         Number of bytes to retrieve per call (default: 10)\n");
    printf("  -t, --timeout MS        Timeout in milliseconds (default: 0 = no timeout)\n");
    printf("  -l, --log               Log output to stdout (default: enabled)\n");
    printf("  -q, --quiet             Disable logging to stdout\n");
    printf("  -h, --help              Show this help message\n");
}

// Function to print the octets in hexadecimal format
void print_octets(const uint8_t *octets, size_t len, int should_log) {
    if (!should_log) return;
    
    printf("Generated Octets (%zu bytes): ", len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", octets[i]);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus *bus = NULL;
    int ret;

    // Default values
    int iterations = 1;
    uint32_t num_bytes = 10;
    uint64_t timeout_ms = 0;
    int log_to_stdout = 1;

    // Command line option parsing
    static struct option long_options[] = {
        {"iterations", required_argument, 0, 'n'},
        {"bytes",      required_argument, 0, 'b'},
        {"timeout",    required_argument, 0, 't'},
        {"log",        no_argument,       0, 'l'},
        {"quiet",      no_argument,       0, 'q'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "n:b:t:lqh", long_options, NULL)) != -1) {
        switch (c) {
            case 'n':
                iterations = atoi(optarg);
                if (iterations <= 0) {
                    fprintf(stderr, "Error: iterations must be positive\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'b':
                num_bytes = (uint32_t)atoi(optarg);
                if (num_bytes == 0) {
                    fprintf(stderr, "Error: bytes must be positive\n");
                    return EXIT_FAILURE;
                }
                break;
            case 't':
                timeout_ms = (uint64_t)atoll(optarg);
                break;
            case 'l':
                log_to_stdout = 1;
                break;
            case 'q':
                log_to_stdout = 0;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case '?':
                print_usage(argv[0]);
                return EXIT_FAILURE;
            default:
                abort();
        }
    }

    // Connect to the session bus
    ret = sd_bus_open_user(&bus);
    if (ret < 0) {
        fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-ret));
        goto cleanup;
    }

    if (log_to_stdout) {
        printf("Starting %d iterations, %u bytes per call, timeout: %lu ms\n", 
               iterations, num_bytes, timeout_ms);
    }

    // Perform the specified number of iterations
    for (int i = 0; i < iterations; i++) {
        // Clear any previous error/reply
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        reply = NULL;
        error = SD_BUS_ERROR_NULL;

        if (log_to_stdout && iterations > 1) {
            printf("Iteration %d/%d: ", i + 1, iterations);
            fflush(stdout);
        }

        // Make a method call
        ret = sd_bus_call_method(
            bus,
            "lv.lumii.trng",                         // Service to contact
            "/lv/lumii/trng/SourceXorAggregator",    // Object path
            "lv.lumii.trng.Rng",                     // Interface name
            "ReadBytes",                             // Method name
            &error,                                  // Location to store errors
            &reply,                                  // Reply message
            "tt",                                    // Input signature: 't' for uint64
            num_bytes,                               // Input argument
            timeout_ms                               // timeout in ms
        );

        if (ret < 0) {
            fprintf(stderr, "Failed to issue method call (iteration %d): %s\n", 
                    i + 1, error.message);
            goto cleanup;
        }

        // Parse the reply message
        uint32_t status;
        ret = sd_bus_message_read(reply, "i", &status);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse reply message: %s\n", strerror(-ret));
            goto cleanup;
        }

        if (status != 0) {
            fprintf(stderr, "Failed to issue method call (iteration %d): %s\n", 
                    i + 1, error.message);
            goto cleanup;
        }

        // Parse the reply message
        const void *ptr;
        size_t octets_len;
        ret = sd_bus_message_read_array(reply, 'y', &ptr, &octets_len);
        if (ret < 0) {
            fprintf(stderr, "Failed to read array (iteration %d): %s\n", 
                    i + 1, strerror(-ret));
            goto cleanup;
        }
        
        const uint8_t *octets = ptr;
        if (iterations == 1) {
            print_octets(octets, octets_len, log_to_stdout);
        } else if (log_to_stdout) {
            printf("received %zu bytes\n", octets_len);
        }
    }

    if (log_to_stdout) {
        printf("Completed %d iterations successfully\n", iterations);
    }

cleanup:
    // Free resources
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
