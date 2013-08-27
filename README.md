fs2 (Fernando Silva File System)
===

Trabalho 1 da disciplina MO806 (at IC/Unicamp) : Linux Filesystem que separa palavras de um arquivo.

O fs2 é um sistema de arquivos de "brinquedo" que implementa as funcionalidades de read e write de arquivos. Esse sistema de arquivos é baseado no islenefs e no lwnfs. Dessa forma, as funções de mount e create file também estão implementadas.

Esse sistema de arquivos foi desenvolvido para trabalhar apenas com arquivos de texto. Ao escrever em um arquivo de texto, as palavras são tokenizadas e cada uma é armazenada numa entrada em uma lista duplamente ligada, em memória. Por outro lado, ao ler o i-ésimo caractere, o sistema de arquivo retorna, na verdade, a i-ésima palavra existente no arquivo.

Para fins demonstrativos, a implementação atual somente permite a leitura da primeira posição do arquivo. Porém, um controle interno ajusta de tal forma que a cada leitura a próxima palavra do arquivo é exibida.

A demonstração de seu funcionamento pode ser feita ao executar uma máquina virtual pelo Qemu, usando um kernel compilado com esse sistema de arquivos. Com o qemu instalado e usando a VM disponibilizada por Glauber de Oliveira Costa para a disciplina MO806 (http://www.ic.unicamp.br/~islene/2s2013-mo806/mo806.img), basta seguir as etapas abaixo:

$ qemu-system-i386 -s -S -kernel linux-3.10.5/arch/i386/boot/bzImage -append "ro root=/dev/hda" -hda mo806.img

Os parâmetros -s -S são utilizados para debug. Nesse caso, basta acessar o gdb:

gdb vmlinux
(gdb) target remote :1234
(gdb) b fs2_init
(gdb) c

Em seguida, dentro da VM Qemu, deve-se montar o filesystem:

$ dd if=/dev/zero of=rep bs=1k count=4
$ mount -t fs2 -o loop rep mnt

Por fim, basta acessar o diretório montado, criar e ler um arquivo. A leitura por palavra pode ser observada ao executar o comando "cat" várias vezes, e a cada leitura a palavra seguinte é exibida:
$ cd mnt
$ echo "teste teste2 teste3 teste4" > teste.txt
$ cat teste.txt
teste
$ cat teste.txt
teste2
$ cat teste.txt
teste3
