#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSER_STACK_SIZE 2048
#define SETTING_SIZE 16

/*
 * Not const: a const array goes to flash, which user threads can read.
 * Aligned like the settings table entries, so the key sits at a whole entry index.
 */
static char device_key[32] __aligned(16) = "EC-PRIVATE-KEY:8f3a91c25d7e04b6";

/* Const, so it is in flash: readable by the parser with or without isolation. */
static const char settings[][SETTING_SIZE] __aligned(16) = {
	"led=off",
	"interval=10",
	"mode=auto",
};

K_THREAD_STACK_DEFINE(parser_stack, PARSER_STACK_SIZE);
static struct k_thread parser_thread;

/*
 * Parses one command received from the network.
 * Supported command: "get=<n>", returns setting number n.
 */
static void parse_command(const char *cmd)
{
	if (strncmp(cmd, "get=", 4) == 0) {
		long index = strtol(cmd + 4, NULL, 10);
		char value[SETTING_SIZE];

		/* Bug: the index comes from the packet and is not checked against the table size. */
		memcpy(value, settings[index], sizeof(value));
		printk("parser: setting %ld = %.16s\n", index, value);
		return;
	}

	printk("parser: unknown command '%s'\n", cmd);
}

/*
 * Parser thread: receives three packets and parses them.
 * The first is a normal command, the other two are an attacker's commands,
 * each reading 16 bytes of the key.
 */
static void parser_entry(void *attack_index, void *p2, void *p3)
{
	/* On this thread's stack, so the parser can always access them. */
	char packets[3][32];

	strcpy(packets[0], "get=0");
	snprintf(packets[1], sizeof(packets[1]), "get=%ld", (long)attack_index);
	snprintf(packets[2], sizeof(packets[2]), "get=%ld", (long)attack_index + 1);

	for (int i = 0; i < ARRAY_SIZE(packets); i++) {
		printk("parser: received '%s'\n", packets[i]);
		parse_command(packets[i]);
	}
}

/* Starts the parser thread. */
int main(void)
{
	/* The attacker would compute this index from the firmware's ELF file. */
	long attack_index = ((const char *)device_key - (const char *)settings) / SETTING_SIZE;

	/* K_USER: the parser handles untrusted input, so it runs in user mode */
	k_thread_create(&parser_thread, parser_stack, PARSER_STACK_SIZE,
			parser_entry, (void *)attack_index, NULL, NULL,
			5, K_USER, K_NO_WAIT);
	return 0;
}
