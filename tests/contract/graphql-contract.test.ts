import { describe, expect, it } from 'vitest';
import { checkAll, checkDocument, loadSchema, onlyComments } from './graphql-contract.ts';

const schema = loadSchema();

describe('GraphQL contract (WP-04)', () => {
    it('every app document and recorder query is valid against the schema', () => {
        const results = checkAll(schema);
        expect(results.filter((r) => r.file.includes('src/core/graphql')).length).toBeGreaterThanOrEqual(9);
        expect(results.filter((r) => r.problems.length > 0)).toEqual([]);
    });

    it('catches unknown fields, wrong argument types and bad syntax', () => {
        expect(checkDocument(schema, 'X.graphql', 'query X { aboutServer { nmae } }', true).join()).toMatch(/nmae/);
        expect(checkDocument(schema, 'X.graphql', 'query X { manga(id: "1") { id } }', true).length).toBeGreaterThan(0);
        expect(checkDocument(schema, 'X.graphql', 'query X($u: Int!) { aboutServer { name } }', true).join()).toMatch(/\$u/);
        expect(checkDocument(schema, 'X.graphql', 'query X { aboutServer {', true)[0]).toMatch(/syntax error/);
    });

    it('app documents: no deprecated fields, one operation named like the file', () => {
        expect(checkDocument(schema, 'X.graphql', 'query X { aboutServer { revision } }', true).join()).toMatch(/deprecated/);
        // The recorder's queries may use deprecated fields (they only record what the server says).
        expect(checkDocument(schema, 'X.graphql', 'query X { aboutServer { revision } }', false)).toEqual([]);
        expect(checkDocument(schema, 'Y.graphql', 'query X { aboutServer { name } }', true).join()).toMatch(/named like the file/);
        expect(checkDocument(schema, 'X.graphql', 'query X { aboutServer { name } } query Z { aboutServer { name } }', true).join())
            .toMatch(/exactly one operation/);
        expect(checkDocument(schema, 'X.graphql', '{ aboutServer { name } }', true).join()).toMatch(/named like the file/);
    });

    it('a file with only comments is a pointer to an app document', () => {
        expect(onlyComments('# document: Manga\n# vars: {"id": 1}\n\n')).toBe(true);
        expect(onlyComments('# x\nquery X { a }')).toBe(false);
    });
});
