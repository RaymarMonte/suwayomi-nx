// Contract test (docs/TESTING.md layer 2, WP-04): every GraphQL document the app sends (src/core/graphql/*.graphql, embedded
// into the app at build time) must be valid against the server's schema (tests/fixtures/schema.graphql, dumped from the pinned
// server in WP-02). So a typo in a field name, a wrong argument type or a removed field fails here, on the PC, in seconds.
// App documents also must not use deprecated fields, must hold exactly one operation, and that operation must be named like the
// file (the build step and gql::document() find them by file name). The fixture recorder's own queries
// (tools/record-fixtures/queries/) are checked against the schema too; files there that only point at an app document are skipped.
//
// Usage: node tests/contract/graphql-contract.ts   (npm run contract; tools/build.sh tests runs it after the unit tests)

import { readdirSync, readFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
    buildSchema,
    type DocumentNode,
    type GraphQLSchema,
    Kind,
    NoDeprecatedCustomRule,
    parse,
    specifiedRules,
    validate,
} from 'graphql';
import { isMain } from '../e2e/lib/cli.ts';

const REPO = fileURLToPath(new URL('../../', import.meta.url));
export const SCHEMA_FILE = join(REPO, 'tests/fixtures/schema.graphql');
export const APP_DOCUMENTS = join(REPO, 'src/core/graphql');
export const RECORDER_QUERIES = join(REPO, 'tools/record-fixtures/queries');

export interface FileResult {
    file: string;
    problems: string[];
}

export function loadSchema(file = SCHEMA_FILE): GraphQLSchema {
    return buildSchema(readFileSync(file, 'utf8'));
}

/** True when the text has nothing but comments and blank lines. */
export function onlyComments(text: string): boolean {
    return text.split('\n').every((line) => /^\s*(#.*)?$/.test(line));
}

/**
 * The problems of one document, empty if it is fine. `app` adds the app's own rules (no deprecated fields, one operation
 * named like the file).
 */
export function checkDocument(schema: GraphQLSchema, file: string, text: string, app: boolean): string[] {
    let doc: DocumentNode;
    try {
        doc = parse(text);
    } catch (e) {
        return [`syntax error: ${(e as Error).message}`];
    }
    const rules = app ? [...specifiedRules, NoDeprecatedCustomRule] : specifiedRules;
    const problems = validate(schema, doc, rules).map((e) => e.message);
    if (app) {
        const operations = doc.definitions.filter((d) => d.kind === Kind.OPERATION_DEFINITION);
        const expected = basename(file, '.graphql');
        if (operations.length !== 1) {
            problems.push(`must hold exactly one operation, found ${operations.length}`);
        } else if (operations[0].kind === Kind.OPERATION_DEFINITION && operations[0].name?.value !== expected) {
            problems.push(`the operation must be named like the file: "${expected}", found "${operations[0].name?.value ?? '(none)'}"`);
        }
    }
    return problems;
}

function graphqlFiles(dir: string): string[] {
    return readdirSync(dir).filter((f) => f.endsWith('.graphql')).sort().map((f) => join(dir, f));
}

/** Checks every app document and every recorder query. */
export function checkAll(schema = loadSchema()): FileResult[] {
    const results: FileResult[] = [];
    for (const file of graphqlFiles(APP_DOCUMENTS)) {
        results.push({ file, problems: checkDocument(schema, file, readFileSync(file, 'utf8'), true) });
    }
    for (const file of graphqlFiles(RECORDER_QUERIES)) {
        const text = readFileSync(file, 'utf8');
        if (onlyComments(text)) continue;  // "# document: <Name>": records an app document, checked above
        results.push({ file, problems: checkDocument(schema, file, text, false) });
    }
    return results;
}

function main(): void {
    const results = checkAll();
    let failed = 0;
    for (const { file, problems } of results) {
        const name = file.slice(REPO.length);
        if (problems.length === 0) {
            console.log(`ok    ${name}`);
        } else {
            failed++;
            console.log(`FAIL  ${name}`);
            for (const p of problems) console.log(`        ${p}`);
        }
    }
    const appCount = results.filter((r) => r.file.startsWith(APP_DOCUMENTS)).length;
    if (appCount === 0) {
        console.log(`no documents found in ${APP_DOCUMENTS}`);
        process.exit(1);
    }
    console.log(`contract: ${results.length - failed}/${results.length} documents valid against tests/fixtures/schema.graphql`);
    process.exit(failed === 0 ? 0 : 1);
}

if (isMain(import.meta.url)) main();
