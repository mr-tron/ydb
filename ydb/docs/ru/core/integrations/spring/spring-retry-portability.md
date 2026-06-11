# Перенос кода Spring Data JPA и Spring Data JDBC на {{ ydb-short-name }}

{% note warning %}

Описанный здесь модуль [spring-ydb-retry](https://github.com/ydb-platform/ydb-java-dialects/tree/main/spring-ydb-retry) является экспериментальным. Его API и поведение могут измениться в будущих версиях без сохранения обратной совместимости.

{% endnote %}

При переносе существующего приложения на {{ ydb-short-name }} обычно хочется решить две задачи одновременно:

- **Безопасность.** {{ ydb-short-name }} — распределённая база данных, поэтому часть ошибок носит временный характер (переключение лидера таблетки, перегрузка, недоступность узла, устаревшая сессия). Корректная обработка таких ошибок требует повтора транзакции целиком, а не отдельного запроса.
- **Переносимость.** Уже написанный код, использующий `@Transactional` поверх [Spring Data JPA](https://spring.io/projects/spring-data-jpa) или [Spring Data JDBC](https://spring.io/projects/spring-data-jdbc), желательно перенести без переписывания бизнес-логики.

Модуль `spring-ydb-retry` решает обе задачи: он подключается к стандартному механизму транзакций Spring и добавляет автоматический повтор, не требуя изменений в существующем коде. Подробное описание самого модуля — в разделе [{#T}](spring-retry.md).

## Почему код остаётся переносимым {#portability}

Модуль не вводит собственный программный интерфейс работы с данными. Он работает на уровне инфраструктуры транзакций Spring:

- При наличии зависимости в classpath автоконфигурация заменяет стандартный бин `transactionInterceptor` на реализацию с логикой повтора.
- Перехватываются те же методы, что и обычно, — помеченные `@Transactional`. Никаких специальных аннотаций или базовых классов добавлять не требуется.
- Решение о повторе принимается по [кодам статуса {{ ydb-short-name }}](../../reference/ydb-sdk/error_handling.md). Для других СУБД эти коды не возникают, поэтому при запуске того же кода на другой базе данных модуль не выполняет повторов и остаётся прозрачным.

В результате один и тот же код репозиториев и сервисов работает и со Spring Data JPA, и со Spring Data JDBC, и при необходимости — с другой реляционной СУБД.

{% note info %}

Повтор выполняется на внешней границе транзакции. Метод, который присоединяется к уже открытой транзакции (`PROPAGATION_REQUIRED` при активной транзакции), отдельно не повторяется — повторяется вызов верхнего уровня, открывший транзакцию.

{% endnote %}

## Подготовка {#setup}

Помимо `spring-ydb-retry` и [{{ ydb-short-name }} JDBC Driver](https://github.com/ydb-platform/ydb-jdbc-driver) (см. [установку](spring-retry.md#install)), потребуется выбранный слой доступа к данным:

- для Spring Data JPA — диалект Hibernate для {{ ydb-short-name }}, см. [{#T}](../orm/hibernate.md);
- для Spring Data JDBC — диалект {{ ydb-short-name }}, см. [{#T}](../orm/spring-data-jdbc.md).

Источник данных настраивается одинаково для обоих вариантов:

```properties
spring.datasource.driver-class-name=tech.ydb.jdbc.YdbDriver
spring.datasource.url=jdbc:ydb:<grpc/grpcs>://<host>:<2135/2136>/path/to/database[?saFile=file:~/sa_key.json]
```

## Использование с существующим кодом {#usage}

Бизнес-код не меняется — повтор подключается автоматически. Достаточно того, что метод помечен `@Transactional`.

{% list tabs %}

- Spring Data JDBC

  ```java
  public interface AccountRepository extends CrudRepository<Account, Long> {
  }

  @Service
  public class TransferService {

      private final AccountRepository accounts;

      public TransferService(AccountRepository accounts) {
          this.accounts = accounts;
      }

      // Существующий код. Аннотация не менялась — повтор добавляется модулем.
      @Transactional
      public void transfer(long fromId, long toId, long amount) {
          Account from = accounts.findById(fromId).orElseThrow();
          Account to = accounts.findById(toId).orElseThrow();
          from.setBalance(from.getBalance() - amount);
          to.setBalance(to.getBalance() + amount);
          accounts.save(from);
          accounts.save(to);
      }
  }
  ```

- Spring Data JPA

  ```java
  public interface AccountRepository extends JpaRepository<Account, Long> {
  }

  @Service
  public class TransferService {

      private final AccountRepository accounts;

      public TransferService(AccountRepository accounts) {
          this.accounts = accounts;
      }

      // Существующий код. Аннотация не менялась — повтор добавляется модулем.
      @Transactional
      public void transfer(long fromId, long toId, long amount) {
          Account from = accounts.findById(fromId).orElseThrow();
          Account to = accounts.findById(toId).orElseThrow();
          from.setBalance(from.getBalance() - amount);
          to.setBalance(to.getBalance() + amount);
      }
  }
  ```

{% endlist %}

При повторяемой ошибке {{ ydb-short-name }} модуль выполнит метод заново в новой транзакции. Для Spring Data JPA это означает, что каждая попытка получает чистый контекст персистентности (`EntityManager`), а изменения предыдущей неуспешной попытки откатываются.

## Когда добавлять @YdbTransactional {#ydb-transactional}

Стандартного `@Transactional` достаточно для базового повтора временных ошибок. Если для конкретного метода нужен более тонкий контроль, добавьте `@YdbTransactional` с нужными параметрами повтора:

```java
@YdbTransactional(maxRetries = 5, idempotent = true)
public Account reload(long id) {
    return accounts.findById(id).orElseThrow();
}
```

Использование `@YdbTransactional` точечно, только там, где нужны его дополнительные возможности, сохраняет переносимость остального кода.

## Идемпотентность и безопасность повтора {#idempotency}

Повтор означает повторное выполнение всего транзакционного метода, поэтому он безопасен только для идемпотентных операций.

- Без `idempotent = true` повторяются только заведомо временные коды статуса, для которых известно, что транзакция гарантированно не была применена.
- С `idempotent = true` дополнительно повторяются недетерминированные коды статуса (например, когда результат коммита неизвестен). Включайте этот режим только для операций, повторное выполнение которых безопасно.

Полная таблица кодов статуса и уровней задержки приведена в разделе [{#T}](spring-retry.md#policy).

{% note warning %}

Повтор работает через прокси Spring AOP, поэтому распространяется только на внешние вызовы транзакционных методов. Вызов транзакционного метода изнутри того же бина (self-invocation) не перехватывается — это стандартное ограничение прокси-режима Spring.

{% endnote %}

## Смотрите также {#see-also}

* [{#T}](spring-retry.md)
* [{#T}](../orm/spring-data-jdbc.md)
* [{#T}](../orm/hibernate.md)
* [{#T}](../../reference/ydb-sdk/error_handling.md)
