#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>

// Function declarations
void print_octets(const uint8_t *octets, size_t len, int should_log);

// Structure to track request state
typedef struct {
    int request_id;
    uint32_t expected_bytes;
    int log_to_stdout;
    int total_iterations;
} request_context_t;

// Global counters for async operations
static int completed_requests = 0;
static int failed_requests = 0;

// Callback function for async D-Bus method calls
static int async_callback(sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    request_context_t *ctx = (request_context_t *)userdata;
    int ret;

    if (ret_error && sd_bus_error_is_set(ret_error)) {
        fprintf(stderr, "Failed to issue method call (request %d): %s\n", 
                ctx->request_id, ret_error->message);
        failed_requests++;
        free(ctx);
        return 0;
    }

    // Parse the reply message
    uint32_t status;
    ret = sd_bus_message_read(reply, "i", &status);
    if (ret < 0) {
        fprintf(stderr, "Failed to parse reply message (request %d): %s\n", 
                ctx->request_id, strerror(-ret));
        failed_requests++;
        free(ctx);
        return 0;
    }

    if (status != 0) {
        fprintf(stderr, "Method call returned error status (request %d): %d\n", 
                ctx->request_id, status);
        failed_requests++;
        free(ctx);
        return 0;
    }

    // Parse the octets array
    const void *ptr;
    size_t octets_len;
    ret = sd_bus_message_read_array(reply, 'y', &ptr, &octets_len);
    if (ret < 0) {
        fprintf(stderr, "Failed to read array (request %d): %s\n", 
                ctx->request_id, strerror(-ret));
        failed_requests++;
        free(ctx);
        return 0;
    }

    if (octets_len != ctx->expected_bytes) {
        fprintf(stderr, "Received %zu bytes, expected %u bytes (request %d)\n", 
                octets_len, ctx->expected_bytes, ctx->request_id);
        failed_requests++;
        free(ctx);
        return 0;
    }

    const uint8_t *octets = ptr;
    
    // Log the result
    if (ctx->total_iterations == 1) {
        print_octets(octets, octets_len, ctx->log_to_stdout);
    } else if (ctx->log_to_stdout) {
        printf("Request %d: received %zu bytes\n", ctx->request_id, octets_len);
    }

    completed_requests++;
    free(ctx);
    return 0;
}

// Function to print usage information
void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -n, --iterations NUM    Number of D-Bus calls to make (default: 1)\n");
    printf("  -b, --bytes NUM         Number of bytes to retrieve per call (default: 10)\n");
    printf("  -c, --concurrent NUM    Number of concurrent in-flight requests (default: 1)\n");
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
    int concurrent = 1;
    uint64_t timeout_ms = 0;
    int log_to_stdout = 1;

    // Command line option parsing
    static struct option long_options[] = {
        {"iterations", required_argument, 0, 'n'},
        {"bytes",      required_argument, 0, 'b'},
        {"concurrent", required_argument, 0, 'c'},
        {"timeout",    required_argument, 0, 't'},
        {"log",        no_argument,       0, 'l'},
        {"quiet",      no_argument,       0, 'q'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "n:b:c:t:lqh", long_options, NULL)) != -1) {
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
            case 'c':
                concurrent = atoi(optarg);
                if (concurrent <= 0) {
                    fprintf(stderr, "Error: concurrent must be positive\n");
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
        printf("Starting %d iterations, %u bytes per call, %d concurrent requests, timeout: %lu ms\n", 
               iterations, num_bytes, concurrent, timeout_ms);
    }

    // Reset global counters
    completed_requests = 0;
    failed_requests = 0;

    // Use synchronous calls if concurrent is 1, otherwise use async
    if (concurrent == 1) {
        // Original synchronous implementation
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

            if (octets_len != num_bytes) {
                fprintf(stderr, "Received %zu bytes, expected %u bytes\n", octets_len, num_bytes);
                ret = -1;
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
    } else {
        // Async implementation for concurrent requests
        int requests_sent = 0;
        int in_flight = 0;

        while (requests_sent < iterations || in_flight > 0) {
            // Send new requests up to the concurrency limit
            while (requests_sent < iterations && in_flight < concurrent) {
                request_context_t *ctx = malloc(sizeof(request_context_t));
                if (!ctx) {
                    fprintf(stderr, "Failed to allocate memory for request context\n");
                    ret = -ENOMEM;
                    goto cleanup;
                }

                ctx->request_id = requests_sent + 1;
                ctx->expected_bytes = num_bytes;
                ctx->log_to_stdout = log_to_stdout;
                ctx->total_iterations = iterations;

                sd_bus_slot *slot = NULL;
                ret = sd_bus_call_method_async(
                    bus,
                    &slot,
                    "lv.lumii.trng",                         // Service to contact
                    "/lv/lumii/trng/SourceXorAggregator",    // Object path
                    "lv.lumii.trng.Rng",                     // Interface name
                    "ReadBytes",                             // Method name
                    async_callback,                          // Callback function
                    ctx,                                     // User data
                    "tt",                                    // Input signature
                    num_bytes,                               // Input argument
                    timeout_ms                               // timeout in ms
                );

                if (ret < 0) {
                    fprintf(stderr, "Failed to issue async method call (request %d): %s\n", 
                            ctx->request_id, strerror(-ret));
                    free(ctx);
                    goto cleanup;
                }

                requests_sent++;
                in_flight++;

                if (log_to_stdout && iterations > 1) {
                    printf("Sent request %d/%d\n", requests_sent, iterations);
                }
            }

            // Process events
            ret = sd_bus_process(bus, NULL);
            if (ret < 0) {
                fprintf(stderr, "Failed to process bus: %s\n", strerror(-ret));
                goto cleanup;
            }

            // Update in-flight counter
            int total_processed = completed_requests + failed_requests;
            in_flight = requests_sent - total_processed;

            // If we have pending events, continue processing
            if (ret > 0) {
                continue;
            }

            // Wait for events if we still have requests in flight
            if (in_flight > 0) {
                ret = sd_bus_wait(bus, (uint64_t) -1);
                if (ret < 0) {
                    fprintf(stderr, "Failed to wait on bus: %s\n", strerror(-ret));
                    goto cleanup;
                }
            }
        }

        if (log_to_stdout) {
            printf("Completed %d requests (%d successful, %d failed)\n", 
                   iterations, completed_requests, failed_requests);
        }

        if (failed_requests > 0) {
            ret = -1;
            goto cleanup;
        }
    }

cleanup:
    // Free resources
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
