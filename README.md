```
                                           ,  
                                  _       ((_,-.
                        .-.      / \       '-.\_)'-,
                       /   \    /^./\__       )  _ )'-
          _        .--'\/\_ \__/.      \.....(/(/ \));,.
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

This extension implements a postgres background worker which builds generic statistical models.  

It's kind of like autovacuum but updating parameter vectors rather than removing dead tuples.

## Usage

Alps will modify all tables in the target database and add new fields.  The fields are the names of existing fields on a table concatenated by "__predicted".  Thus if you have a table of housing prices, ...

```
select id, sq_feet, zipcode, price
```

Alps would add a column *price__predicted*

A common use for filling in nulls is...

```
select id, coalesce(price, price__predicted);
```

This prototype of Alps will attempt to create __predicted columns for any boolean or numeric field (numeric, float, int).


## Important!

NEVER run alps on a transactional production system.

This extension adds columns, modifies the database on the fly, and runs as a priviledged user.  It is inherently unsafe and should only be run on replicated servers used for reporting which can be rebuilt -- at this time.

## Installation

On a system with all dependencies installation is pretty straightforward (see next section for dependency installation).  

### Download and install

```
git clone git@github.com:no0p/alps.git
cd alps
make
make install
```

### Initialize extension

```
psql
#> create extension alps;
```

### Update configurations

Edit postgresql.conf line...

```
shared_preload_libraries = 'alps'
alps.target_db = 'my_database' 
```

Then restart postgresql server.


## Setting up Environment

Requires >= pg 9.3  (see http://apt.postgresql.org)

Requires madlib installed. (see https://github.com/madlib/madlib)

At the time of writing use https://github.com/no0p/madlib to build madlib as 9.3 support is waiting in PR upstream.

## Configuring

For automatic execution Alps requires autovacuum to be turned on and auto analyze.

Alps is triggered to re-train models for tables after tables are analyzed.  So you can wait for the table to be auto analyzed, or you can analyze the table and the next alps pass will result in re-training the models.

May require max_locks_per_transaction to be increased... working on relaxing this.


## About

Extracted from work on http://relsys.io



