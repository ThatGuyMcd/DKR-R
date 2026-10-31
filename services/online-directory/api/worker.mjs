import library, { HttpError } from './library_api.mjs';
import { handleLauncherProfile, launcherVerifiedSocial } from './profile_api.mjs';

export default {
  scheduled(event,env,ctx) { return library.scheduled(event,env,ctx); },
  async fetch(request, env, ctx) {
    try {
      const profile = await handleLauncherProfile(request, env);
      if(profile)return profile;
      const url=new URL(request.url);
      if(url.pathname==='/v1/online/capabilities' && request.method==='GET') {
        const response=await library.fetch(request,env,ctx);
        if(!response.ok)return response;
        return Response.json({...await response.json(),socialProfiles:true},{status:response.status,headers:response.headers});
      }
      if(env.ONLINE_DIRECTORY && url.pathname.startsWith('/v1/online/')) {
        // Authentication and ticket handling stay with the existing API.
        // Only the authenticated service-binding ingress receives social metadata.
        const binding=env.ONLINE_DIRECTORY;
        const directory={async fetch(input) {
          const forwarded=input instanceof Request?input:new Request(input);
          const headers=new Headers(forwarded.headers);
          headers.delete('X-DKRR-Social');
          const account=headers.get('X-DKRR-Account');
          if(account) {
            const profile=await launcherVerifiedSocial(env,account,new URL(forwarded.url).searchParams.get('profile'));
            if(profile)headers.set('X-DKRR-Social',encodeURIComponent(JSON.stringify(profile)));
          }
          return binding.fetch(new Request(forwarded,{headers}));
        }};
        return library.fetch(request,{...env,ONLINE_DIRECTORY:directory},ctx);
      }
      return library.fetch(request,env,ctx);
    } catch(error) {
      return Response.json({error:{code:error instanceof HttpError?error.code:'profile_service',
        message:error instanceof HttpError?error.message:'The profile service could not complete this request. Please try again.'}},
        {status:error instanceof HttpError?error.status:500,headers:{'Cache-Control':'no-store','Access-Control-Allow-Origin':'https://positron-portal.com'}});
    }
  }
};
