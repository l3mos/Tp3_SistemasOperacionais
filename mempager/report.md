<!-- LTeX: language=pt-BR -->

# PAGINADOR DE MEMÓRIA -- RELATÓRIO

1. Termo de compromisso

    Ao entregar este documento preenchiso, os membros do grupo afirmam que todo o código desenvolvido para este trabalho é de autoria própria.  Exceto pelo material listado no item 3 deste relatório, os membros do grupo afirmam não ter copiado material da Internet nem ter obtido código de terceiros.

2. Membros do grupo e alocação de esforço

    Preencha as linhas abaixo com o nome e o email dos integrantes do grupo.  Substitua marcadores `XX` pela contribuição de cada membro do grupo no desenvolvimento do trabalho (os valores devem somar 100%).

    * Nome1 <email1> XX%
    * Nome2 <email2> XX%

3. Referências bibliográficas

4. Detalhes de implementação
    1. Estruturas de Dados Utilizadas
PageEntry
Descrição: Esta estrutura representa uma entrada na tabela de páginas de um processo. Contém informações sobre a validade da página (is_valid), o número do quadro associado (frame_number), o bloco de disco associado (disk_block), se a página foi modificada (is_modified), e o endereço virtual da página (virtual_address).
Justificativa: Facilita o mapeamento entre as páginas virtuais e os quadros físicos, além de rastrear modificações e alocação no disco.
ProcessMemoryTable
Descrição: Representa a tabela de páginas de um processo, contendo um array de PageEntry, o identificador do processo (pid), o número de páginas alocadas (page_count), e a capacidade da tabela (table_capacity).
Justificativa: Mantém o controle das páginas alocadas para cada processo, permitindo a expansão dinâmica da tabela de páginas.
FrameEntry
Descrição: Representa uma entrada na tabela de quadros de memória física. Contém o identificador do processo ao qual o quadro está associado (pid), uma flag de acesso (access_flag), e um ponteiro para a entrada da página (page_entry).
Justificativa: Essencial para implementar o algoritmo de substituição de páginas, onde cada quadro físico armazena informações sobre qual página ele está mapeando.
PhysicalMemoryTable
Descrição: Estrutura que mantém o controle da memória física, incluindo o número total de quadros (total_frames), o tamanho das páginas (size_of_page), o ponteiro do relógio para o algoritmo de substituição (clock_pointer), e um array de FrameEntry que representa os quadros.
Justificativa: Permite gerenciar a memória física e implementar o algoritmo de segunda chance para a substituição de páginas.
DiskEntry
Descrição: Representa uma entrada na tabela de blocos de disco, indicando se o bloco está em uso (is_used) e qual página está associada ao bloco (associated_page).
Justificativa: Facilita o mapeamento entre as páginas que foram trocadas para o disco e os blocos de disco onde elas estão armazenadas.
DiskMemory
Descrição: Estrutura que mantém o controle do armazenamento em disco, contendo o número total de blocos (total_blocks) e um array de DiskEntry que representa os blocos de disco.
Justificativa: Essencial para gerenciar as páginas que foram movidas para o disco, permitindo recuperar essas páginas quando necessário.
    2. Mecanismo de Controle de Acesso e Modificação
Controle de Acesso: O controle de acesso às páginas é realizado através de flags e operações de proteção de memória (PROT_READ, PROT_WRITE, PROT_NONE). Quando ocorre uma falha de página, a função resolve_page_fault ajusta as permissões de acesso da página conforme necessário.

Modificação de Páginas: A modificação das páginas é rastreada pela flag is_modified em PageEntry. Se uma página foi modificada, ela é escrita de volta ao disco antes de ser substituída. Isso é realizado na função swap_out_page, onde a página é escrita no disco se necessário.
    3. Descrição das Funções Principais
pager_init
Descrição: Inicializa as estruturas de dados necessárias para gerenciar a memória física e o disco. Aloca memória para as tabelas de quadros e blocos, além de configurar o ponteiro do relógio para o algoritmo de substituição.
Justificativa: Esta função prepara todas as estruturas de dados necessárias antes que qualquer operação de paginação possa ser realizada.
pager_create
Descrição: Cria uma nova entrada para o processo na tabela de memória do sistema. Expande a tabela se necessário para acomodar novos processos.
Justificativa: Garantir que cada processo tenha sua própria tabela de páginas, permitindo o gerenciamento independente de memória para cada processo.
pager_extend
Descrição: Aloca uma nova página para o processo, associando-a a um bloco de disco livre. A página ainda não é mapeada para um quadro de memória física até que seja acessada.
Justificativa: Implementa a política de adiamento de trabalho, onde a alocação física da página é adiada até o momento em que a página é realmente acessada.
pager_fault
Descrição: Lida com falhas de página, determinando se a página precisa ser carregada do disco para a memória ou se as permissões de acesso precisam ser ajustadas.
Justificativa: Essencial para o gerenciamento dinâmico da memória, garantindo que as páginas estejam presentes e acessíveis quando necessário.
pager_syslog
Descrição: Copia os dados de uma página na memória e os imprime em formato hexadecimal. Se a página não estiver presente na memória física, retorna um erro.
Justificativa: Permite que o sistema registre atividades de memória para fins de depuração ou auditoria.
pager_destroy
Descrição: Libera todos os recursos alocados para um processo específico, removendo suas páginas da memória física e liberando os blocos de disco associados.
Justificativa: Necessário para evitar vazamentos de memória e garantir que os recursos de memória e disco sejam reutilizados quando um processo termina.
