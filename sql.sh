#!/bin/bash

# test against original

# flux sql 'SELECT eventlog FROM jobs'
# flux sql 'SELECT json_extract(eventlog, "$[0]") FROM jobs'

# fail
#flux sql 'SELECT eventlog FROM jobs WHERE id = 58988691456 and json_valid(eventlog) and json_extract(eventlog, "$[0].name") = "submit"'

# flux sql 'SELECT json_extract(eventlog, "$[0]") FROM jobs WHERE id = 58988691456 and json_valid(eventlog)'

# flux sql 'SELECT eventlog FROM jobs as T1 WHERE T1.id = 58988691456 and json_extract(T1.eventlog, "$[0].name") = "submit"'

# flux sql 'SELECT eventlog FROM jobs as T1 WHERE T1.id = 58988691456 and json_extract(T1.eventlog, "$[0].context.userid") = 8556'

# flux sql 'SELECT T1.* FROM jobs as T1, json_each(T1.eventlog) as T2 WHERE json_extract(T2.value, "$.name") = "submit" and json_extract(T2.value, "$.context.userid") = 8556'

# flux sql 'SELECT T1.* FROM jobs as T1, json_each(T1.eventlog) as T2 WHERE json_extract(T2.value, "$.name") = "submit" and json_extract(T2.value, "$.context.userid") = 8556'

# flux sql 'SELECT json_extract(T2.value, "$.context.userid") as userid FROM jobs as T1, json_each(T1.eventlog) as T2 WHERE json_extract(T2.value, "$.name") = "submit" and json_extract(T2.value, "$.context.userid") = 8556'

# ***
# flux sql 'SELECT T1.id FROM jobs as T1, json_each(T1.eventlog) as T2 WHERE json_extract(T2.value, "$.context.userid") = 8556'

# flux sql 'SELECT * FROM jobs'



# test against updated eventlog

# flux sql 'SELECT id FROM jobs WHERE json_extract(jobs.eventlog, "$.submit.context.userid") = 8556 AND json_extract(jobs.eventlog, "$.finish.context.status") = 0 AND json_extract(jobs.jobspec, "$.attributes.system.duration") = 0'

flux sql 'SELECT id FROM jobs WHERE json_extract(jobs.eventlog, "$.submit.context.userid") = 8556 AND json_extract(jobs.eventlog, "$.finish.context.status") = 0 AND (json_extract(jobs.jobspec, "$.tasks[0].command[0]") = "hostname" OR json_extract(jobs.jobspec, "$.attributes.system.job.name") = "foo")'
