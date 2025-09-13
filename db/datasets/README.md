# Datasets for Testing

## Sakila Database

The Sakila sample database is provided by MySQL and represents a DVD rental store. It includes:

- **47 tables** with realistic business relationships
- **~1000 films** in the catalog
- **16,000+ customers** with rental history
- **Payment transactions** and inventory management
- **Complex relationships** with foreign keys and constraints

### Files

- `sakila-db/sakila-schema.sql` - Database structure (tables, views, functions, triggers)
- `sakila-db/sakila-data.sql` - Sample data for all tables
- `sakila-db/sakila.mwb` - MySQL Workbench model file

### Usage

The Sakila database is used for:
1. **Integration testing** - Real-world complexity testing
2. **Performance benchmarks** - Testing with realistic data volumes
3. **Complex query validation** - JOINs, aggregations, subqueries
4. **Data integrity testing** - Foreign keys, transactions, constraints

### Source

Downloaded from: https://downloads.mysql.com/docs/sakila-db.zip
Official documentation: https://dev.mysql.com/doc/sakila/en/

### License

The Sakila sample database is released under the New BSD license.