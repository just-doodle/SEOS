#include "process.h"
#include "elf_loader.h"
#include "shell.h"
#include "compositor.h"
#include "keyboard.h"

list_t *process_list;
pcb_t *current_process;
pcb_t *last_process;

pid_t curr_pid;
registers_t saved_context;

pid_t alloc_pid()
{
    return curr_pid++;
}

void context_switch(registers_t *p_regs, context_t *n_regs)
{
    if (last_process && last_process != current_process)
    {
        last_process->regs.eax = p_regs->eax;
        last_process->regs.ebx = p_regs->ebx;
        last_process->regs.ecx = p_regs->ecx;
        last_process->regs.edx = p_regs->edx;
        last_process->regs.esi = p_regs->esi;
        last_process->regs.edi = p_regs->edi;
        last_process->regs.ebp = p_regs->ebp;
        last_process->regs.esp = p_regs->useresp;
        last_process->regs.eflags = p_regs->eflags;
        last_process->regs.eip = p_regs->eip;
        asm volatile("mov %%cr3, %0" : "=r"(last_process->regs.cr3));
    }

    if (((page_directory_t *)n_regs->cr3) != NULL)
    {
        switch_page_dir((page_directory_t *)n_regs->cr3, 1);
    }

    pic_eoi(0);
    last_process = current_process;

    if (current_process->type == TASK_TYPE_KERNEL)
    {
        kernel_regs_switch(n_regs);
    }
    else if (current_process->type == TASK_TYPE_USER)
    {
        user_regs_switch(n_regs);
    }
}

pcb_t* get_process_by_pid(pid_t pid)
{
    foreach(t, process_list)
    {
        pcb_t *pcb = (pcb_t *)t->val;

        if(pcb->pid == pid)
        {
            return pcb;
        }
    }
    return NULL;
}

pid_t getpid()
{
    return current_process->pid;
}

int kill(pid_t pid, uint32_t sig)
{
    if(pid == current_process->pid)
    {
        exit(sig);
    }

    pcb_t* p = get_process_by_pid(pid);
    printf("[PMGR] Process %d killed with code %d.\n", pid, sig);
    serialprintf("[PMGR] Process %d killed with code %d.\n", pid, sig);

    p->state = TASK_STOPPED;

    if(p->page_dir != NULL)
    {
        kfree(p->page_dir);
    }

    list_remove_node(process_list, p->self);

    kfree(p);

    last_process = NULL;

    return 0;
}

void change_process(pid_t pid)
{
    pcb_t *pcb = get_process_by_pid(pid);

    if(pid == 0)
    {
        if(pcb == NULL)
        {
            kernel_panic("[PMGR] No process left! Never exit userspace init process...\n");
        }
    }

    while(pcb == NULL)
    {
        pcb = get_process_by_pid(pid--);
    }

    if(pcb->state == TASK_STOPPED)
    {
        pcb = get_process_by_pid((pid == 0 ? 0 : (pid--)));
    }

    current_process = pcb;

    context_switch(&saved_context, &(pcb->regs));
}

void exit(uint32_t ret)
{
    pid_t pid = current_process->pid;

    printf("[PMGR] Process %d exited with code %d.\n", pid, ret);
    serialprintf("[PMGR] Process %d exited with code %d.\n", pid, ret);

    current_process->state = TASK_STOPPED;

    for (size_t i = 0; i < current_process->args.argc; i++)
    {
        free(current_process->args.argv[i]);
    }

    free(current_process->args.argv);
    

    if(current_process->page_dir != NULL)
    {
        serialprintf("PDA: 0x%06x\n", current_process->page_dir_addr);
        //kfree((void*)current_process->page_dir_addr);
    }

    list_remove_node(process_list, current_process->self);

    kfree(current_process);

    last_process = NULL;

    change_process((pid == 0 ? 0 : (pid--)));
}

void create_process_from_routine(char *name, void *entrypoint, uint32_t type)
{
    pcb_t *pcb = ZALLOC_TYPES(pcb_t);

    strcpy(pcb->filename, name);
    pcb->pid = alloc_pid();

    pcb->entrypoint = (uint32_t)entrypoint;

    pcb->regs.esp = 0xC0000000;
    pcb->regs.eflags = 0x206;
    pcb->regs.eip = pcb->entrypoint;

    pcb->page_dir = kmalloc_a(sizeof(page_directory_t));
    memset(pcb->page_dir, 0, sizeof(page_directory_t));
    pcb->page_dir_addr = (uint32_t)pcb->page_dir;

    copy_page_dir(pcb->page_dir, kernel_page_dir);
    alloc_region(pcb->page_dir, 0xC0000000 - 4 * PAGE_SIZE, 0xC0000000, 0, 0, 1);
    pcb->regs.cr3 = (uint32_t)virt2phys(kernel_page_dir, pcb->page_dir);

    pcb->type = type;
    pcb->state = TASK_CREATED;

    pcb->handler = 0;

    pcb->self = list_insert_front(process_list, pcb);

    printf("[PMGR] Created process %d: %s.\n", pcb->pid, pcb->filename);
    serialprintf("[PMGR] Created process %d: %s.\n", pcb->pid, pcb->filename);

    use_handler(0);

    change_process(pcb->pid);
}

void execve(char* file, char** argv, char** env)
{
    pcb_t * p1 = ZALLOC_TYPES(pcb_t);
    p1->pid = alloc_pid();
    p1->regs.eip = (uint32_t)load_elf;
    p1->regs.eflags = 0x206;
    p1->self = list_insert_front(process_list, p1);
    strcpy(p1->filename, file);

    p1->type = TASK_TYPE_KERNEL;

    p1->stack = (void*)0xC0000000;
    p1->regs.esp = (0xC0000000 - 4 * 1024);

    p1->page_dir = kmalloc_a(sizeof(page_directory_t));
    memset(p1->page_dir, 0, sizeof(page_directory_t));
    p1->page_dir_addr = (uint32_t)p1->page_dir;
    copy_page_dir(p1->page_dir, kernel_page_dir);
    p1->regs.cr3 = (uint32_t)virt2phys(kernel_page_dir, p1->page_dir);

    for(int j = 0; argv[j] != NULL; j++)
    {
        p1->args.argc++;
    }
    p1->args.argc++;
    p1->args.argv = zalloc(sizeof(uint32_t) * (p1->args.argc + 1));

    int i;
    for(i = 0; i < p1->args.argc; i++)
    {
        if(argv[i] == NULL)
            break;
        p1->args.argv[i+1] = strdup(argv[i]); 
    }
    p1->args.argv[i+1] = NULL;
    p1->args.argv[0] = strdup(p1->filename);
    p1->args.argv[i+2] = NULL; 

    p1->state = TASK_CREATED;

    use_handler(0);

    change_process(p1->pid);
}

pargs_t a;

pargs_t* get_args()
{
    serialprintf("ARGS: ");
    for(int i= 0; i < current_process->args.argc; i++)
        serialprintf("\"%s\":%d\n", current_process->args.argv[i], strlen(current_process->args.argv[i]));
    memset(&a, 0, sizeof(pargs_t));
    a = current_process->args;
    return(&a);
}

void create_process(char* file)
{
    pcb_t * p1 = ZALLOC_TYPES(pcb_t);
    p1->pid = alloc_pid();
    p1->regs.eip = (uint32_t)load_elf;
    p1->regs.eflags = 0x206;
    p1->self = list_insert_front(process_list, p1);
    strcpy(p1->filename, file);

    p1->type = TASK_TYPE_KERNEL;

    p1->stack = (void*)0xC0000000;
    p1->regs.esp = (0xC0000000 - 4 * 1024);

    p1->page_dir = kmalloc_a(sizeof(page_directory_t));
    memset(p1->page_dir, 0, sizeof(page_directory_t));
    p1->page_dir_addr = (uint32_t)p1->page_dir;
    copy_page_dir(p1->page_dir, kernel_page_dir);
    p1->regs.cr3 = (uint32_t)virt2phys(kernel_page_dir, p1->page_dir);
    p1->state = TASK_CREATED;

    change_process(p1->pid);
}

void attach_handler(process_kbhandler_t handler)
{
    current_process->handler = handler;
}

void init_processManager()
{
    process_list = list_create();
    init_keyboard();
    change_keyboard_handler(process_kbh);
    printf("[ PMGR ] Process manager initialized...\n");
}

void list_process()
{
    printf("No. of processes: %d\n", curr_pid);
    foreach (t, process_list)
    {
        pcb_t *p = t->val;
        printf("%s%d : Name: %s, Type: %s, State: %s\n", (current_process->pid == p->pid ? ">" : " "), p->pid, p->filename, (p->type == TASK_TYPE_KERNEL ? "Kernel task" : "Userspace task"), (p->state == TASK_CREATED ? "Created" : (p->state == TASK_RUNNING ? "Running" : (p->state == TASK_LOADING ? "Loading" : "Unknown"))));
    }
}

int isPause = 0;

void process_kbh(uint8_t scancode)
{
    char key;
    int isCTRL = 0;
    int isALT = 0;

    if (scancode < 80)
    {
        switch (scancode)
        {
        case 0x46: //Scroll lock
            isPause = !isPause;
            break;
        case 0x4B:
            if(isPause)
            {
                window_t* f = get_focused_window();
                if(f->self->prev->val != NULL)
                    window_focus(f->self->prev->val);
                window_drawall();
            }break;
        case 0x4D:
            if(isPause)
            {
                window_t* f = get_focused_window();
                if(f->self->next->val != NULL)
                    window_focus(f->self->next->val);
                window_drawall();
            }break;
        case 0xFA:
            break;
        case 0x3B:
            if(isPause)
            {
                printf("Reboot!\n");
                serialprintf("Reboot!\n");
                reboot();
                isPause = false;
            }
            break;
        case 0x3C:
            if(isPause)
            {
                printf("[PMGR] Stopping process %d\n", current_process->pid);
                isPause = false;
                exit(12);
            }
            break;
        case 0x3D:
            if (isPause)
            {
                list_process();
                isPause = false;
            }
            break;
        case 0x3E:
            if(isPause)
            {
                less_exception();
                isPause = false;
            }
            break;
        case 0x3F:
            if(isPause)
            {
                list_descriptors();
                isPause = false;
            }
            break;
        };
    }

    key = kcodeTochar(scancode);

    process_kbhandler_t handler = current_process->handler;

    if (handler == 0)
    {
        return;
    }

    handler(key, isCTRL, isALT, scancode);
}