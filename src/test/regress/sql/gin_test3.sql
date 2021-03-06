-- gin 创建 修改 重建 删除 测试

-- Set GUC paramemter
SET ENABLE_SEQSCAN=OFF;
SET ENABLE_INDEXSCAN=OFF;
SET ENABLE_BITMAPSCAN=ON;


-- 普通表
DROP TABLE IF EXISTS test_gin_1;
CREATE TABLE test_gin_1 (id INT, info INT[]);
DROP TABLE IF EXISTS test_gin_2;
CREATE TABLE test_gin_2 (id INT, first_name text, last_name text);

-- 创建索引
CREATE INDEX  test_gin_1_idx ON test_gin_1 USING GIN(info);
CREATE INDEX  test_gin_2_first_name_idx  ON test_gin_2 USING GIN(to_tsvector('ngram', first_name));
CREATE INDEX  test_gin_2_first_last_name_idx ON test_gin_2 USING GIN(to_tsvector('ngram', first_name || last_name));

-- 数据导入
INSERT INTO test_gin_1 SELECT g, ARRAY[1, g % 5, g] FROM generate_series(1, 200) g;
INSERT INTO test_gin_2 SELECT id, md5(random()::text), md5(random()::text) FROM
          (SELECT * FROM generate_series(1,100) AS id) AS x;

-- 查询
SELECT * FROM test_gin_1 WHERE info @> '{2}' AND info @> '{22}' ORDER BY id, info;
SELECT * FROM test_gin_1 WHERE info @> '{22}' OR info @> '{32}' ORDER BY id, info;
SELECT * FROM test_gin_2 WHERE to_tsvector('ngram', first_name) @@ to_tsquery('ngram', 'test') ORDER BY id, first_name, last_name;
SELECT * FROM test_gin_2 WHERE to_tsvector('ngram', first_name || last_name) @@ to_tsquery('ngram', 'test') ORDER BY id, first_name, last_name;

-- 索引更新
-- 重命名索引
ALTER INDEX IF EXISTS test_gin_1_idx RENAME TO test_gin_2_idx;
ALTER INDEX IF EXISTS test_gin_2_idx RENAME TO test_gin_1_idx;

-- 设置索引storage_parameter
ALTER INDEX IF EXISTS test_gin_1_idx SET (FASTUPDATE =OFF);
\d+ test_gin_1_idx
ALTER INDEX IF EXISTS test_gin_1_idx RESET (FASTUPDATE);
\d+ test_gin_1_idx
ALTER INDEX IF EXISTS test_gin_1_idx SET (FASTUPDATE =ON);
\d+ test_gin_1_idx

-- 设置索引不可用
ALTER INDEX test_gin_1_idx UNUSABLE;

INSERT INTO test_gin_1 SELECT g, ARRAY[1, g % 5, g] FROM generate_series(1, 200) g;

-- rebuild索引

ALTER INDEX test_gin_1_idx REBUILD; 

SELECT * FROM test_gin_1 WHERE info @> '{22}' ORDER BY id, info;

SELECT * FROM test_gin_1 WHERE info @> '{22}' AND info @> '{2}' AND info @> '{1}' ORDER BY id, info;

-- 删除索引
DROP INDEX IF EXISTS test_gin_1_idx;

--- 索引表
DROP TABLE test_gin_1;
DROP TABLE test_gin_2;

RESET ENABLE_SEQSCAN;
RESET ENABLE_INDEXSCAN;
RESET ENABLE_BITMAPSCAN;
