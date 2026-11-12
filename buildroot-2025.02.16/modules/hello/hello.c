#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julia Kriedte");
MODULE_DESCRIPTION("KMOD Exemplo");
MODULE_VERSION("0.0.1");

static int hello_init(void)
{
    pr_emerg("KERN_EMERG: mensagens de emergência, normalmente antes de uma falha completa do sistema.\n");
    pr_alert("KERN_ALERT: uma situação que requer ação imediata.\n");
    pr_crit("KERN_CRIT: condições críticas, geralmente relacionadas a falhas de software ou hardware.\n");
    pr_err("KERN_ERR: condições de erro; drivers usam para reportar problemas no hardware.\n");
    pr_warn("KERN_WARNING: avisos sobre situações problemáticas que não criam, em si, problemas sérios.\n");
    pr_notice("KERN_NOTICE: situações normais, porém dignas de nota, como condições de segurança.\n");
    pr_info("KERN_INFO: mensagens informativas, como as de inicialização de um driver.\n");
    pr_debug("KERN_DEBUG: mensagens de depuração.\n");
    // A mensagem de KERN_DEBUG não aparece. Isso ocorre porque o kernel mantém um console loglevel, que 
    // define o nível mínimo de prioridade que uma mensagem deve ter para ser impressa automaticamente no 
    // console. Apenas mensagens mais prioritárias que esse nível (ou seja, com valor numérico menor) 
    // são impressas.

    return 0;
}

static void hello_exit(void)
{
    pr_alert("Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_exit);