// Asks the running test server for its GraphQL schema (introspection) and writes it as SDL to
// tests/fixtures/schema.graphql, plus the server's version to tests/fixtures/server-version.txt.
// Contract tests check our .graphql documents against that file, offline (docs/TESTING.md layer 2).
//
// Usage (test server running): node tools/dump-schema/dump-schema.ts
//   Server address and auth come from SNX_SERVER_URL / SNX_AUTH / ... (tools/test-server/test-server.sh env), else defaults.

import { writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import {
    buildClientSchema, getIntrospectionQuery, getNamedType, isObjectType, isScalarType, printSchema, type IntrospectionQuery,
} from 'graphql';
import { fail, isMain } from '../../tests/e2e/lib/cli.ts';
import { optionsFromEnv, SuwayomiClient, waitForServer } from '../../tests/e2e/lib/suwayomi.ts';

const FIXTURES = fileURLToPath(new URL('../../tests/fixtures/', import.meta.url));

// aboutServer fields we'd like in server-version.txt, in this order (only the ones the server has are asked for).
const ABOUT_FIELDS = ['name', 'version', 'buildType', 'revision', 'buildTime'];

async function main(): Promise<void> {
    const client = new SuwayomiClient(optionsFromEnv());
    await waitForServer(client);
    // Only introspection features every GraphQL server has, so an older server can be dumped too.
    const introspection = await client.query<IntrospectionQuery>(getIntrospectionQuery({
        descriptions: true, specifiedByUrl: false, directiveIsRepeatable: false, schemaDescription: false,
        inputValueDeprecation: false, oneOf: false,
    }));
    const schema = buildClientSchema(introspection);

    const aboutType = getNamedType(schema.getQueryType()?.getFields().aboutServer?.type);
    if (!aboutType || !isObjectType(aboutType)) fail('the server has no aboutServer query');
    const fields = ABOUT_FIELDS.filter((f) => {
        const field = aboutType.getFields()[f];
        return field !== undefined && isScalarType(getNamedType(field.type));
    });
    const about = (await client.query<{ aboutServer: Record<string, unknown> }>(`{ aboutServer { ${fields.join(' ')} } }`)).aboutServer;
    const version = String(about.version ?? 'unknown');

    const header = `# GraphQL schema of Suwayomi-Server ${version}, dumped by tools/dump-schema. Don't edit by hand: re-run the tool.\n\n`;
    await writeFile(`${FIXTURES}schema.graphql`, header + printSchema(schema) + '\n');
    await writeFile(`${FIXTURES}server-version.txt`, fields.map((f) => `${f}: ${String(about[f])}`).join('\n') + '\n');
    const types = Object.keys(schema.getTypeMap()).filter((t) => !t.startsWith('__')).length;
    console.log(`schema: ${types} types, server ${version} -> tests/fixtures/schema.graphql, server-version.txt`);
}

if (isMain(import.meta.url)) await main();
