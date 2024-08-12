<!-- LTeX: language=pt-BR -->

# PAGINADOR DE MEMÓRIA -- RELATÓRIO

1. Termo de compromisso

    Ao entregar este documento preenchiso, os membros do grupo afirmam que todo o código desenvolvido para este trabalho é de autoria própria.  Exceto pelo material listado no item 3 deste relatório, os membros do grupo afirmam não ter copiado material da Internet nem ter obtido código de terceiros.

2. Membros do grupo e alocação de esforço

    Preencha as linhas abaixo com o nome e o email dos integrantes do grupo.  Substitua marcadores `XX` pela contribuição de cada membro do grupo no desenvolvimento do trabalho (os valores devem somar 100%).

    * Carolina Brandão Farinha Baeta <carolinabrandao@ufmg.br> 33.3%
    * Daniel Andrade Carmo <danielcarmo@ufmg.br> 33.3%
    * Victor de Almeida Nunes Murta <victormurta@ufmg.br> 33.3%

3. Referências bibliográficas
    * GeeksforGeeks. "Second Chance (or Clock) Page Replacement Policy" Disponível em: https://www.geeksforgeeks.org/second-chance-or-clock-page-replacement-policy/ . Acesso em: 08/08/2024.

    * Silberschatz, A., Galvin, P. B., & Gagne, G. (2015). Fundamentos de Sistemas Operacionais.

    * Aulas da disciplina de Sistemas Operacionais, ministradas pelo Professor Ítalo Cunha, no primeiro semestre de 2024.

4. Detalhes de implementação

    1. Descreva e justifique as estruturas de dados utilizadas em sua solução.
    2. Descreva o mecanismo utilizado para controle de acesso e modificação às páginas.


1. Estruturas de dados

    Struct VirtualPage: A estrutura representa uma página virtual associada a um processo. Ela contém os campos valid (indica se a página é válida), frame (associa a página a um quadro de memória física), block (associa a página a um bloco de disco), modified (indica se a página foi modificada), e address (endereço virtual da página). Precisamos dela para rastrear o estado de cada página virtual e para associar páginas virtuais a quadros de memória física e blocos de disco.

    Struct ProcessMemory: Esta ED serve para estruturar as páginas virtuais de um processo. Ela contém um PID, um vetor de VirtualPages, um contador e um inteiro que gerencia a quantidade de páginas virtuais alocadas para o processo. Precisamos dela para manter um registro de todas as páginas que pertencem a um processo específico, facilitando a alocação, liberação e o tratamento de page faults.

    Struct MemoryFrame: A estrutura representa um quadro de memória física. Ela contém os campos PID, um inteiro access_bit que indica se o quadro foi ou não acessado (para o algoritmo da segunda chance) e um ponteiro para a VirtualPage que está mapeada para este quadro. É necessário para gerenciar a memória física e facilitar a implementação do algoritmo de substituição de páginas, monitorando o acesso a cada quadro e a página que está mapeada nele.

    Struct PhysicalMemory: Esta estrutura gerencia os quadros de memória física disponíveis no sistema. Contém um int size (tamanho total da memória), um int page_size (tamanho de cada página), um vetor de MemoryFrames e um int clock_hand, utilizado no algoritmo da segunda chance para implementar o comportamento cíclico de verificação dos acessos. clock_hand serve como um índice que aponta para o quadro de memória atual que está sendo considerado pelo algoritmo de substituição de páginas. O índice circula por pela lista de quadros da memória, garantindo o comportamento cíclico do algoritmo da segunda chance. A estrutura PhysicalMemory permite o controle sobre a alocação e substituição de páginas em memória, crucial para implementar o comportamento de memória virtual esperado no sistema. 

    Struct DiskBlock:  Representa um bloco de disco onde páginas podem ser armazenadas quando não há memória física suficiente. Contém um campo used para indicar se o bloco está em uso, e um ponteiro para a VirtualPage associada. É necessária para a implementação da paginação em disco, permitindo que páginas que não cabem na memória física sejam armazenadas temporariamente no disco.

    Struct DiskStorage: Gerencia os blocos de disco disponíveis no sistema. Contém um contador de blocos totais e um vetor de DiskBlocks. É necessário para controlar a alocação e liberação de blocos de disco, permitindo que páginas sejam armazenadas e recuperadas do disco conforme necessário.

2. Controle de acesso e modificação

    Algumas estratégias foram utilizadas em conjunto para garantir o funcionamento adequado e seguro. 


    Mutex: Foi utilizado um mutex (pthread_mutex_t global_lock) para garantir que as operações de acesso e modificação sejam realizadas de maneira segura, previnindo condições de corrida quando múltiplos processos tentam acessar ou modificar as páginas ao mesmo tempo.


    Bits de controle: 
    access_bit: Foi utilizado para monitorar o uso de uma página em um frame físico no algoritmo de segunda chance. Nesse contexto, esse bit é crucial para decidir se uma página deve ser mantida na memória ou se pode ser substituída.


    modified: Foi utilizado para monitorar se uma página foi modificada. É importante para o bom funcionamento, pois, se uma página foi modificada, ela deve ser escrita de volta no disco antes de ser removida do frame.


    Algoritmo da segunda chance: (função clock_algorithm()). Quando o paginador precisa de um quadro livre para tratar uma falha de acesso, mas não há quadros livres disponíveis, o algoritmo da segunda chance é utilizado para selecionar qual página será removida. O paginador verifica o access_bit das páginas candidatas à remoção. Se o bit estiver setado para 1, a página recebe uma segunda chance: o bit é resetado para 0, e as permissões de acesso (leitura e escrita) são removidas temporariamente, usando "mmu_chprot". Na próxima vez que a página for acessada, o paginador será notificado devido à falta de permissões, permitindo que ele reatribua as permissões necessárias. Se uma página for encontrada com o access_bit em 0, e não for modificada (modified em 0), ela será escolhida para remoção, liberando o quadro de memória física.