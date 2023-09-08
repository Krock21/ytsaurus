# Шардирование

В данном разделе рассматриваются способы шардирования динамических таблиц. Приводится описание алгоритма автоматического шардирования.

Динамические таблицы делятся на [таблеты](../../../user-guide/dynamic-tables/overview.md#tablets), таблет является единицей параллелизма. . Для равномерного распределения нагрузки можно воспользоваться  стандартным приёмом: добавить первой вспомогательную ключевую колонку, в которую записывать хеш от той части ключа, по которой выполнять шардирование (например, хеш от первой колонки). В результате получится таблица, ключи которой равномерно распределены в диапазоне `[0, 2^64-1]`.

Для разбиения такой таблицы на k таблетов достаточно разбить диапазон `[0, 2^64-1]` на k частей.

Значение ключевой колонки можно вычислять на клиентской стороне и передавать при записи в таблицу, но в качестве альтернативы можно использовать вычисляемые колонки.

## Вычисляемые колонки { #expression }

Система {{product-name}} поддерживает возможность автоматического вычисления значения ключевой колонки по формуле. Данную формулу необходимо указать в схеме этой колонки в поле `expression`. Ключевая колонка может зависеть только от невычисляемых ключевых колонок. При записи строки или поиска строки по ключу вычисляемые колонки необходимо пропускать.

Для равномерного распределения будет лучше указать `"expression" = "farm_hash(key)"`, где `key` — префикс исходного ключа (`farm_hash` — это встроенная функция, вычисляющая [FarmHash](https://code.google.com/p/farmhash) от аргументов).

При использовании автоматически вычисляемых колонок стоит учитывать, что для оптимизации работы операция `select_rows` выводит из предиката диапазон затрагиваемых ключей. Если в предикате некоторые значения вычисляемых колонок не заданы явно, то {{product-name}} попробует дополнить условие значением вычисляемых колонок. В текущей реализации результат будет успешным, только в том случае если те колонки, от которых зависит вычисляемая, в предикате определяются равенством или с помощью оператора `IN`. Вычисление явно заданных значений вычисляемых колонок в выводе диапазонов не происходит.

## Пример использования вычисляемых колонок

Пусть имеется таблица с колонками `hash, key, value`, причём `hash` и `key` ключевые, а для `hash` в схеме указана формула `expression = "farm_hash(key)"`. Тогда для операций вставки, удаления и чтения по ключу нужно указывать только `key` и `value`. Чтобы операция `select_rows` работала эффективно, в предикате нужно точно определить `key`, тогда система {{product-name}} сможет вычислить какие значения `hash` нужно рассматривать. Например, в запросе можно указать `WHERE key = key_value` или `WHERE key IN (key_value1, key_value2)`.

Если же указать `WHERE key > key_lower_bound and key < key_upper_bound`, то, диапазон для `hash` вывести нельзя. В некоторых случаях возможен перебор значений вычисляемых колонок. Перебор происходит в следующих случаях:

1. Выражение вычисляемой колонки имеет вид `expression = "f(key / divisor)"` , при этом `key` и `divisor` должны быть целочисленными. В данном случае происходит перебор всех таких значений `key`, которые порождают различные значения `expression`. Такое поведение обобщается на случай с несколькими вычисляемыми колонками и несколькими вхождениями `key`c различными делителями.
2. Выражение имеет вид `expression = "f(key) % mod"`. В данном случае происходит перебор значений `expression` в пределах значения `mod`, а также в перебор включается значение `null`.

Если существует возможность переборов обоими способами, выбирается тот, который порождает наименьшее количество значений. Общее количество ключей, порождаемых перебором, ограничено параметром `range_expansion_limit`.

## Автоматическое шардирование { #auto }

Шардирование необходимо для равномерного распределения нагрузки по кластеру. Автоматическое шардирование включает в себя:

1. Шардирование таблиц.
2. Перераспределение таблетов между [таблет-селлами](../../../user-guide/dynamic-tables/overview.md#tablet_cells).

Шардирование необходимо для того, чтобы таблеты у таблицы стали примерно одинакового размера. Перераспределение между таблет-селлами — чтобы на таблет-селлы приходилось примерно поровну данных. Данное обстоятельство особенно важно для таблиц, находящихся в памяти (c `@in_memory_mode` отличным от `none`), так как память на кластере весьма ограниченный ресурс и при неудачном распределении некоторые узлы кластера могут озазаться перегружены.

Балансировку можно настраивать как потаблично, так и для каждого [таблет-селл бандла](../../../user-guide/dynamic-tables/overview.md#tablet_cell_bundles).

Список доступных настроек для бандла хранится по пути `
//sys/tablet_cell_bundles/<bundle_name>/@tablet_balancer_config`, а также представлен в таблице.

| Имя             | Тип  | Значение по умолчанию | Описание                         |
| -------------------------------- | --------- | ---------------------- | ------------------------------------------------------------ |
| min_tablet_size        | int   | 128 MB         | Минимальный размер таблета                  |
| desired_tablet_size      | int   | 10 GB         | Желаемый размер таблета                   |
| max_tablet_size        | int   | 20 GB         | Максимальный размер таблета                 |
| min_in_memory_tablet_size   | int   | 512 MB         | Минимальный размер таблета in memory таблицы         |
| desired_in_memory_tablet_size | int   | 1 GB          | Желаемый размер таблета in memory таблицы          |
| max_in_memory_tablet_size   | int   | 2 GB          | Максимальный размер таблета in memory таблицы        |
| enable_tablet_size_balancer  | boolean | %true         | Включение/отключение шардирования             |
| enable_in_memory_cell_balancer | boolean | %true         | Включение/отключение перемещения in-memory таблетов между селлами |
| enable_cell_balancer      | boolean | %false         | Включение/отключение перемещения не-in-memory таблетов между селлами |

Список доступных настроек для таблицы (`//path/to/table/@tablet_balancer_config`) приведён в таблице:

| Имя       | Тип  | Значение по умолчанию | Описание                         |
| ------------------------- | --------- | ---------------------- | ------------------------------------------------------------ |
| enable_auto_reshard   | boolean | %true         | Включение/отключение шардирования             |
| enable_auto_tablet_move | boolean | %true         | Включение/отключение перемещения таблетов таблицы между селлами |
| min_tablet_size     | int   | -           | Минимальный размер таблета                  |
| desired_tablet_size   | int   | -           | Желаемый размер таблета                   |
| max_tablet_size     | int   | -           | Максимальный размер таблета                 |
| desired_tablet_count  | int   | -           | Желаемое количество таблетов                 |
| min_tablet_count | int | - | Минимальное количество таблетов (смотрите пояснение в тексте) |

{% cut  "Устаревшие атрибуты таблицы" %}

Ранее вместо `//path/to/table/@tablet_balancer_config` настройки балансировщика ставились напрямую на таблицу. Так, в таблице можно было найти следующие атрибуты:

- enable_tablet_balancer;
- disable_tablet_balancer;
- min_tablet_size;
- desired_tablet_size;
- max_tablet_size;
- desired_tablet_count.

Данные атрибуты объявлены **deprecated**. Они провязаны с соответствующими значениями из `//path/to/table/@tablet_balancer_config`, но их использование **не рекомендуется**.

Ранее опция `enable_tablet_balancer` могла либо отсутствовать, либо принимать одно из значений true/false. Сейчас она однозначно соответствует опции `enable_auto_reshard` и либо содержит `false` (балансировка выключена), либо отсутствует (балансировка включена, значение по умолчанию).

{% endcut %}


Если в настройках таблицы указан параметр `desired_tablet_count`, то балансировщик будет пытаться шардировать таблицу на указанное число таблетов. Иначе, если в настройках таблицы указаны все три параметра `min_tablet_size`, `desired_tablet_size`, `max_tablet_size` и их значения допустимы (т.е. верно `min_tablet_size < desired_tablet_size < max_tablet_size`), то вместо настроек по умолчанию будут использоваться значения указанных параметров. В противном случае будут использоваться настройки таблет-селл бандла.

Алгоритм работы автоматического шардирования следующий: фоновый процесс следит за примонтированными таблетами и как только обнаруживает таблет меньше `min_tablet_size` или больше `max_tablet_size`, то пытается привести его к `desired_tablet_size`, возможно, затронув соседние таблеты. Если указан параметр `desired_tablet_count`, то пользовательские настройки размера таблетов будут проигнорированы, а значения будут вычислены исходя из размера таблицы и `desired_tablet_count`.

Если установлен параметр `min_tablet_count`, балансировщик не будет объединять таблеты, если в результате их количество станет меньше ограничения. Однако эта опция не гарантирует, что балансировщик уменьшит таблеты, если сейчас их слишком мало: при её использовании необходимо предварительно самостоятельно порешардировать таблицу на желаемое число таблетов.

### Отключение автоматического шардирования

На таблице:

- `yt set //path/to/table/@tablet_balancer_config/enable_auto_reshard %false` — отключить шардирование;
- `yt set //path/to/table/@tablet_balancer_config/enable_auto_tablet_move %false` — отключить перемещение между таблетов между селлами.

На таблет-селл бандле:

- `yt set //sys/tablet_cell_bundles//@tablet_balancer_config/enable_tablet_size_balancer %false` — отключить шардирование;
- `yt set //sys/tablet_cell_bundles//@tablet_balancer_config/enable_{in_memory_,}cell_balancer %false` — отключить перемещение между селлами in_memory/не-in_memory таблетов соответственно.

## Расписание автоматического шардирования

Шардирование неизбежно отмонтирует часть таблетов таблицы. Чтобы сделать этот процесс более предсказуемым, существует возможность настроить, в какое время должен работать балансировщик. Настройка бывает per-cluster и per-bundle. Расписание для бандла находится в атрибуте`//sys/tablet_cell_bundles//@tablet_balancer_config/tablet_balancer_schedule`. В качестве формата можно указать любую арифметическую формулу от целочисленных переменных `hours` и `minutes`. Балансировка таблиц бандла будет происходить только в те моменты времени, когда значение формулы истинно (т.е. отлично от нуля).

Фоновый процесс балансировки запускается раз в несколько минут, поэтому стоит рассчитывать, что таблеты могут находиться в отмонтированном состоянии в течение 10 минут после того, как формула становилась истинной.

Примеры:

- `minutes % 20 == 0` — балансировка в 0-ю, 20-ю и 40-ю минуту каждого часа;
- `hours % 2 == 0 && minutes == 30` — балансировка в 00:30, 02:30, ...

Если значение атрибута для бандла не указано, используется значение по умолчанию для кластера. Пример настройки балансировки на уровне кластеров представлен в таблице 3.

| Кластер | Расписание |
| ---------- | ------------------------------------ |
| first-cluster | ` (hours * 60 + minutes) % 40 == 0` |
| second-cluster | ` (hours * 60 + minutes) % 40 == 10` |
| third-cluster | ` (hours * 60 + minutes) % 40 == 20` |