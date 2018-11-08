# DynWebStats

## Introdução

O DynWebStats é um arcabouço para coleta dinâmica de conjuntos de páginas na WEB. Através de estratégias de escalonamento, o arcabouço consegue racionalizar recursos e manter um retrato atualizado do conjunto de páginas que está sendo analisado.

## Arquitetura

O diagrama a seguir mostra o funcionamento básico do DynWebStats:



## Descrição estrutural

Os seguintes arquivos são utilizados no sistema:

|         Arquivo        |                                  Descrição                                            |
|:-----------------------|:--------------------------------------------------------------------------------------|
| lib/dyn\_web\_stats.rb | Código central, responsável por fazer todas as chamadas e gerência dos demais códigos |
| lib/fifo.rb            | Implementação do escalonador FIFO |
| lib/lifo.rb            | Implementação do escalonador LIFO |
| lib/dyn.rb             | Implementação do escalonador Dinâmico utilizando bootstrap |
| lib/heritrix.rb        | Código que encapsula a comunicação com o daemon do Heritrix |
| lib/models.rb          | Define os models do MongoDB utilizados no processo de coleta |
| lib/sift4.rb           | Implementação simplificada da distância de edição sift4 (semelhante à distância Levenshtein) |
| lib/warc.rb            | Código que chama funções de parsear os arquivos WARC gerados pelo heritrix                   |
| extras/warc            | Implementação em C do parser de WARC, utilizada no código acima |
