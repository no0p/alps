```
                                  _
                        .-.      / \        _
                       /   \    /^./\__   _/ \
          _        .--'\/\_ \__/.      \ /    \      ___
         / \_    _/ ^      \/  __  :'   /\/\  /\  __/   \
        /    \  /    .'   _/  /  \   ^ /    \/  \/ .`'\_/\
       /\/\  /\/ :' __  ^/  ^/    `--./.'  ^  `-.\ _    _:\ _
      /    \/  \  _/  \-' __/.' ^ _   \_   .'\   _/ \ .  __/ \
    /\  .-   `. \/     \ / -.   _/ \ -. `_/   \ /    `._/  ^  \
   /  `-.__ ^   / .-'.--'    . /    `--./ .-'  `-.  `-. `.  -  `.
 @/        `.  / /      `-.   /  .-'   / .   .'   \    \  \  .-  \%

                _____  .____   __________  _________
               /  _  \ |    |  \______   \/   _____/
              /  /_\  \|    |   |     ___/\_____  \ 
             /    |    \    |___|    |    /        \
             \____|__  /_______ \____|   /_______  /
                     \/        \/                \/ 


```

## Alps

This extension implements a postgres background worker which builds generic statistical models in the background.  It's kind of like autovacuum but updating parameter vectors rather than removing dead tuples.

## Important -- Danger!

First things first, NEVER run alps on a transactional production system.  Not that you would consider running a piece of software named "alps" on your database in the first place.  But really, don't.

This extension is writes to your database on the fly, and is therefore inherently unsafe and should only be run on replicated servers used for reporting which can be rebuilt.

## Installation

On a system with all dependencies installation is pretty straightforward (see next section for dependency installation).  

```
make
make install
```

Edit postgresql.conf line...

```
shared_preload_libraries = 'alps'
alps.target_db = 'my_database' # After installing and creating extension
```

Then restart postgresql server.


## Setting up Environment

Requires >= pg 9.3  (see http://apt.postgresql.org)

Requires madlib installed. (see https://github.com/madlib/madlib)


## Configuring

For automatic execution Alps requires autovacuum to be turned on and auto analyze.

Alps is triggered to re-train models for tables after tables are analyzed.  So you can wait for the table to be auto analyzed, or you can analyze the table and the next alps pass will result in re-training the models.

If you have many tables may require max_locks_per_transaction...working on relaxing this.

## Usage

Alps will modify all tables in the target database and add new fields.  The fields are the names of existing fields on a table concatenated by "__predicted".  Thus if you have a table of housing prices, ...

```
select id, sq_feet, zipcode, price, price__predicted;
```

Would return the output for the predicted price based on possible support columns in the table.

A common use for filling in nulls is...

```
select id, coalesce(price, price__predicted);
```

## About

Extracted from work on http://relsys.io



