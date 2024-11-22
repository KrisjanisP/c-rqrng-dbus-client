#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <string.h>

// Function to print the octets in hexadecimal format
void print_octets(const uint8_t *octets, size_t len) {
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

    // Number of octets to generate; default to 10
    uint32_t num_octets = 10;

    // Optionally, allow the user to specify the number of octets
    if (argc > 1) {
        num_octets = (uint32_t)atoi(argv[1]);
    }

    // Connect to the session bus
    ret = sd_bus_open_user(&bus);
    if (ret < 0) {
        fprintf(stderr, "Failed to connect to user bus: %s\n", strerror(-ret));
        goto cleanup;
    }

    // Make a method call
    ret = sd_bus_call_method(
        bus,
        "lv.lumii.qrng",                             // Service to contact
        "/lv/lumii/qrng/RemoteQrngXorLinuxRng",      // Object path
        "lv.lumii.qrng.Rng",                         // Interface name
        "GenerateOctets",                            // Method name
        &error,                                      // Location to store errors
        &reply,                                      // Reply message
        "t",                                         // Input signature: 't' for uint32
        num_octets                                   // Input argument
    );

    if (ret < 0) {
        fprintf(stderr, "Failed to issue method call: %s\n", error.message);
        goto cleanup;
    }

    // Parse the reply message
    uint32_t status;

    ret = sd_bus_message_read(reply, "u", &status);
    if (ret < 0) {
        fprintf(stderr, "Failed to parse reply message: %s\n", strerror(-ret));
        goto cleanup;
    }

    if (status == 0) {
        const void *ptr;
        size_t octets_len;
        ret = sd_bus_message_read_array(reply, 'y', &ptr, &octets_len);
        if (ret < 0) {
            fprintf(stderr, "Failed to read array: %s\n", strerror(-ret));
            goto cleanup;
        }
        const uint8_t *octets = ptr;
        print_octets(octets, octets_len);
    } else {
        fprintf(stderr, "GenerateOctets failed with status code: %u\n", status);
    }

cleanup:
    // Free resources
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}